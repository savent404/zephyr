/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_output_dict.h>
#include <zephyr/logging/log_backend_std.h>
#include <zephyr/logging/log_backend_fs.h>
#include <zephyr/kernel.h>
#include <assert.h>
#include <zephyr/fs/fs.h>

#define MAX_PATH_LEN 256
#define MAX_FLASH_WRITE_SIZE 256
#define LOG_PREFIX_LEN (sizeof(CONFIG_LOG_BACKEND_FS_FILE_PREFIX) - 1)
#define MAX_FILE_NUMERAL 9999
#define FILE_NUMERAL_LEN 4

enum backend_fs_state {
	BACKEND_FS_NOT_INITIALIZED = 0,
	BACKEND_FS_CORRUPTED,
	BACKEND_FS_OK
};

static struct fs_file_t fs_file;
static enum backend_fs_state backend_state = BACKEND_FS_NOT_INITIALIZED;
static int file_ctr, newest, oldest;
static uint32_t failure_count;
static int last_error;
static bool backend_degraded;

#ifdef CONFIG_LOG_BACKEND_FS_TESTSUITE
static int fail_next_writes;
static int fail_next_writes_err;
#endif

static int allocate_new_file(struct fs_file_t *file);
static int del_oldest_log(void);
static int get_log_file_id(struct fs_dirent *ent);
static int check_log_file_exist(int num);
static void log_backend_fs_record_failure(int err, void *ctx);
static void log_backend_fs_clear_failure(void);
static int log_backend_fs_recover_file(struct fs_file_t *file, void *ctx);
static ssize_t log_backend_fs_write(struct fs_file_t *file, const void *data, size_t len);
#ifndef CONFIG_LOG_BACKEND_FS_TESTSUITE
static uint32_t log_format_current = CONFIG_LOG_BACKEND_FS_OUTPUT_DEFAULT;
#endif

static void log_backend_fs_warning_print(void)
{
	printk("LOG_BACKEND_FS degraded: failures=%u last_error=%d dir=%s; "
	       "check or remove damaged log files\n",
	       failure_count, last_error, CONFIG_LOG_BACKEND_FS_DIR);
}

static void log_backend_fs_warning_work_handler(struct k_work *work);

K_WORK_DELAYABLE_DEFINE(log_backend_fs_warning_work, log_backend_fs_warning_work_handler);

static void log_backend_fs_warning_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!backend_degraded) {
		return;
	}

	log_backend_fs_warning_print();

	if (CONFIG_LOG_BACKEND_FS_DEGRADE_PRINTK_INTERVAL_MS > 0) {
		(void)k_work_schedule(&log_backend_fs_warning_work,
				      K_MSEC(CONFIG_LOG_BACKEND_FS_DEGRADE_PRINTK_INTERVAL_MS));
	}
}

bool log_backend_fs_is_degraded(void)
{
	return backend_degraded;
}

uint32_t log_backend_fs_failure_count_get(void)
{
	return failure_count;
}

int log_backend_fs_last_error_get(void)
{
	return last_error;
}

static void log_backend_fs_degrade(void *ctx)
{
	const struct log_backend *backend = ctx;

	if (backend_degraded) {
		return;
	}

	backend_degraded = true;
	backend_state = BACKEND_FS_CORRUPTED;

	if (backend != NULL) {
		log_backend_deactivate(backend);
	}

	log_backend_fs_warning_print();
	if (CONFIG_LOG_BACKEND_FS_DEGRADE_PRINTK_INTERVAL_MS > 0) {
		(void)k_work_schedule(&log_backend_fs_warning_work,
				      K_MSEC(CONFIG_LOG_BACKEND_FS_DEGRADE_PRINTK_INTERVAL_MS));
	}
}

static void log_backend_fs_record_failure(int err, void *ctx)
{
	last_error = err;
	failure_count++;

	if (failure_count >= CONFIG_LOG_BACKEND_FS_DEGRADE_FAILURE_THRESHOLD) {
		log_backend_fs_degrade(ctx);
	}
}

static void log_backend_fs_clear_failure(void)
{
	failure_count = 0;
	last_error = 0;
}

static int log_backend_fs_recover_file(struct fs_file_t *file, void *ctx)
{
	int rc;

	(void)fs_close(file);
	rc = check_log_file_exist(newest);
	if ((rc == 0) && (file_ctr > 0)) {
		file_ctr--;
	}

	rc = allocate_new_file(file);
	if (rc < 0) {
		log_backend_fs_record_failure(rc, ctx);
		if (!backend_degraded) {
			backend_state = BACKEND_FS_NOT_INITIALIZED;
		}
		return rc;
	}

	backend_state = BACKEND_FS_OK;
	return 0;
}

static ssize_t log_backend_fs_write(struct fs_file_t *file, const void *data, size_t len)
{
#ifdef CONFIG_LOG_BACKEND_FS_TESTSUITE
	if (fail_next_writes > 0) {
		fail_next_writes--;
		return fail_next_writes_err;
	}
#endif

	return fs_write(file, data, len);
}

#ifdef CONFIG_LOG_BACKEND_FS_TESTSUITE
void log_backend_fs_test_reset(void)
{
	(void)k_work_cancel_delayable(&log_backend_fs_warning_work);
	(void)fs_close(&fs_file);
	fs_file_t_init(&fs_file);
	backend_state = BACKEND_FS_NOT_INITIALIZED;
	file_ctr = 0;
	newest = 0;
	oldest = 0;
	failure_count = 0;
	last_error = 0;
	backend_degraded = false;
	fail_next_writes = 0;
	fail_next_writes_err = -EIO;
}

void log_backend_fs_test_fail_next_writes(int count, int err)
{
	fail_next_writes = count;
	fail_next_writes_err = err;
}

bool log_backend_fs_test_warning_pending(void)
{
	return k_work_delayable_is_pending(&log_backend_fs_warning_work);
}
#endif

static int check_log_volume_available(void)
{
	int index = 0;
	char const *name;
	int rc = 0;

	while (rc == 0) {
		rc = fs_readmount(&index, &name);
		if (rc == 0) {
			if (strncmp(CONFIG_LOG_BACKEND_FS_DIR,
				    name,
				    strlen(name))
			    == 0) {
				return 0;
			}
		}
	}

	return -ENOENT;
}

static int create_log_dir(const char *path)
{
	const char *next;
	const char *last = path + (strlen(path) - 1);
	char w_path[MAX_PATH_LEN];
	int rc, len;
	struct fs_dir_t dir;

	fs_dir_t_init(&dir);

	/* the fist directory name is the mount point*/
	/* the firs path's letter might be meaningless `/`, let's skip it */
	next = strchr(path + 1, '/');
	if (!next) {
		return 0;
	}

	while (true) {
		next++;
		if (next > last) {
			return 0;
		}
		next = strchr(next, '/');
		if (!next) {
			next = last;
			len = last - path + 1;
		} else {
			len = next - path;
		}

		memcpy(w_path, path, len);
		w_path[len] = 0;

		rc = fs_opendir(&dir, w_path);
		if (rc) {
			/* assume directory doesn't exist */
			rc = fs_mkdir(w_path);
			if (rc) {
				break;
			}
		}
		rc = fs_closedir(&dir);
		if (rc) {
			break;
		}
	}

	return rc;

}

int write_log_to_file(uint8_t *data, size_t length, void *ctx)
{
	int rc;
	size_t processed_len = length;
	struct fs_file_t *f = &fs_file;

	if (backend_degraded) {
		return processed_len;
	}

	if (backend_state == BACKEND_FS_NOT_INITIALIZED) {
		if (check_log_volume_available()) {
			return processed_len;
		}
		rc = create_log_dir(CONFIG_LOG_BACKEND_FS_DIR);
		if (!rc) {
			rc = allocate_new_file(&fs_file);
		}
		if (rc) {
			log_backend_fs_record_failure(rc, ctx);
			if (!backend_degraded) {
				backend_state = BACKEND_FS_NOT_INITIALIZED;
			}
			return processed_len;
		}
		backend_state = BACKEND_FS_OK;
	}

	if (backend_state == BACKEND_FS_OK) {

		/* Check if new data overwrites max file size.
		 * If so, create new log file.
		 */
		int size = fs_tell(f);

		if (size < 0) {
			log_backend_fs_record_failure(size, ctx);
			if (!backend_degraded) {
				(void)log_backend_fs_recover_file(f, ctx);
			}
			return processed_len;
		} else if ((size + length) > CONFIG_LOG_BACKEND_FS_FILE_SIZE) {
			rc = allocate_new_file(f);

			if (rc < 0) {
				log_backend_fs_record_failure(rc, ctx);
				return processed_len;
			}
		}

		rc = log_backend_fs_write(f, data, length);
		if (rc >= 0) {
			if (IS_ENABLED(CONFIG_LOG_BACKEND_FS_OVERWRITE) &&
			    (rc != length)) {
				del_oldest_log();

				return processed_len;
			}
			/* If overwrite is disabled, a partial write means the remaining
			 * bytes are dropped. Report the full buffer as processed so
			 * log_output does not retry forever.
			 */
			if (rc != length) {
				length = processed_len;
			} else {
				length = rc;
			}
		} else {
			log_backend_fs_record_failure(rc, ctx);
			if (!backend_degraded) {
				(void)log_backend_fs_recover_file(f, ctx);
			}
			return processed_len;
		}

		rc = fs_sync(f);
		if (rc < 0) {
			log_backend_fs_record_failure(rc, ctx);
			if (!backend_degraded) {
				(void)log_backend_fs_recover_file(f, ctx);
			}
			return processed_len;
		}

		log_backend_fs_clear_failure();
	}

	return length;
}

static int get_log_file_id(struct fs_dirent *ent)
{
	size_t len;
	int num;

	if (ent->type != FS_DIR_ENTRY_FILE) {
		return -1;
	}

	len = strlen(ent->name);

	if (len != LOG_PREFIX_LEN + FILE_NUMERAL_LEN) {
		return -1;
	}

	if (memcmp(ent->name, CONFIG_LOG_BACKEND_FS_FILE_PREFIX, LOG_PREFIX_LEN) != 0) {
		return -1;
	}

	num = atoi(ent->name + LOG_PREFIX_LEN);

	if (num <= MAX_FILE_NUMERAL && num >= 0) {
		return num;
	}

	return -1;
}

static int check_log_file_exist(int num)
{
	struct fs_dir_t dir;
	struct fs_dirent ent;
	int rc;

	fs_dir_t_init(&dir);

	rc = fs_opendir(&dir, CONFIG_LOG_BACKEND_FS_DIR);
	if (rc) {
		return -EIO;
	}

	while (true) {
		rc = fs_readdir(&dir, &ent);
		if (rc < 0) {
			rc = -EIO;
			goto close_dir;
		}
		if (ent.name[0] == 0) {
			break;
		}

		rc = get_log_file_id(&ent);
		if (rc == num) {
			rc = 1;
			goto close_dir;
		}
	}

	rc = 0;

close_dir:
	(void)fs_closedir(&dir);

	return rc;
}

static int allocate_new_file(struct fs_file_t *file)
{
	/* In case of no log file or current file fills up
	 * create new log file.
	 */
	int rc;
	struct fs_statvfs stat;
	int curr_file_num;
	struct fs_dirent ent;
	char fname[MAX_PATH_LEN];
	off_t file_size;

	assert(file);

	if (backend_state == BACKEND_FS_NOT_INITIALIZED) {
		/* Search for the last used log number. */
		struct fs_dir_t dir;
		int file_num = 0;

		fs_dir_t_init(&dir);
		curr_file_num = 0;
		int max = 0, min = MAX_FILE_NUMERAL;

		rc = fs_opendir(&dir, CONFIG_LOG_BACKEND_FS_DIR);

		while (rc >= 0) {
			rc = fs_readdir(&dir, &ent);
			if ((rc < 0) || (ent.name[0] == 0)) {
				break;
			}

			file_num = get_log_file_id(&ent);
			if (file_num >= 0) {

				if (file_num > max) {
					max = file_num;
				}

				if (file_num < min) {
					min = file_num;
				}
				++file_ctr;
			}
		}

		oldest = min;

		if ((file_ctr > 1) &&
		    ((max - min) >
		     2 * CONFIG_LOG_BACKEND_FS_FILES_LIMIT)) {
			/* oldest log is in the range around the min */
			newest = min;
			oldest = max;
			(void)fs_closedir(&dir);
			rc = fs_opendir(&dir, CONFIG_LOG_BACKEND_FS_DIR);

			while (rc == 0) {
				rc = fs_readdir(&dir, &ent);
				if ((rc < 0) || (ent.name[0] == 0)) {
					break;
				}

				file_num = get_log_file_id(&ent);
				if (file_num < min + CONFIG_LOG_BACKEND_FS_FILES_LIMIT) {
					if (newest < file_num) {
						newest = file_num;
					}
				}

				if (file_num > max - CONFIG_LOG_BACKEND_FS_FILES_LIMIT) {
					if (oldest > file_num) {
						oldest = file_num;
					}
				}
			}
		} else {
			newest = max;
			oldest = min;
		}

		(void)fs_closedir(&dir);
		if (rc < 0) {
			goto out;
		}

		curr_file_num = newest;

		/* Is there space left in the newest file? */
		snprintf(fname, sizeof(fname), "%s/%s%04d", CONFIG_LOG_BACKEND_FS_DIR,
			 CONFIG_LOG_BACKEND_FS_FILE_PREFIX, curr_file_num);
		rc = fs_open(file, fname, FS_O_CREATE | FS_O_WRITE | FS_O_APPEND);
		if (rc < 0) {
			goto out;
		}
		file_size = fs_tell(file);
		if (IS_ENABLED(CONFIG_LOG_BACKEND_FS_APPEND_TO_NEWEST_FILE) &&
		    file_size < CONFIG_LOG_BACKEND_FS_FILE_SIZE) {
			/* There is space left to log to the latest file, no need to create
			 * a new one or delete old ones at this point.
			 */
			if (file_ctr == 0) {
				++file_ctr;
			}
			backend_state = BACKEND_FS_OK;
			goto out;
		} else {
			fs_close(file);
			if (file_ctr >= 1) {
				curr_file_num++;
				if (curr_file_num > MAX_FILE_NUMERAL) {
					curr_file_num = 0;
				}
			}
			backend_state = BACKEND_FS_OK;
		}
	} else {
		fs_close(file);

		curr_file_num = newest;
		curr_file_num++;
		if (curr_file_num > MAX_FILE_NUMERAL) {
			curr_file_num = 0;
		}
	}

	rc = fs_statvfs(CONFIG_LOG_BACKEND_FS_DIR, &stat);

	/* Check if there is enough space to write file or max files number
	 * is not exceeded.
	 */
	while ((file_ctr >= CONFIG_LOG_BACKEND_FS_FILES_LIMIT) ||
	       ((stat.f_bfree * stat.f_frsize) <=
		CONFIG_LOG_BACKEND_FS_FILE_SIZE)) {

		if (IS_ENABLED(CONFIG_LOG_BACKEND_FS_OVERWRITE)) {
			rc = del_oldest_log();
			if (rc < 0) {
				goto out;
			}

			rc = fs_statvfs(CONFIG_LOG_BACKEND_FS_DIR,
					&stat);
			if (rc < 0) {
				goto out;
			}
		} else {
			return -ENOSPC;
		}
	}

	snprintf(fname, sizeof(fname), "%s/%s%04d",
		CONFIG_LOG_BACKEND_FS_DIR,
		CONFIG_LOG_BACKEND_FS_FILE_PREFIX, curr_file_num);

	rc = fs_open(file, fname, FS_O_CREATE | FS_O_WRITE);
	if (rc < 0) {
		goto out;
	}
	++file_ctr;
	newest = curr_file_num;

out:
	return rc;
}

static int del_oldest_log(void)
{
	int rc;
	static char dellname[MAX_PATH_LEN];

	while (true) {
		snprintf(dellname, sizeof(dellname), "%s/%s%04d",
			 CONFIG_LOG_BACKEND_FS_DIR,
			 CONFIG_LOG_BACKEND_FS_FILE_PREFIX, oldest);
		rc = fs_unlink(dellname);

		if ((rc == 0) || (rc == -ENOENT)) {
			oldest++;
			if (oldest > MAX_FILE_NUMERAL) {
				oldest = 0;
			}

			if (rc == 0) {
				--file_ctr;
				break;
			}
		} else {
			break;
		}
	}

	return rc;
}

BUILD_ASSERT(!IS_ENABLED(CONFIG_LOG_MODE_IMMEDIATE),
	     "Immediate logging is not supported by LOG FS backend.");

#ifndef CONFIG_LOG_BACKEND_FS_TESTSUITE

static uint8_t __aligned(4) buf[MAX_FLASH_WRITE_SIZE];
LOG_OUTPUT_DEFINE(log_output, write_log_to_file, buf, MAX_FLASH_WRITE_SIZE);

static void log_backend_fs_init(const struct log_backend *const backend)
{
	log_output_ctx_set(&log_output, (void *)backend);
}

static void panic(struct log_backend const *const backend)
{
	/* In case of panic deinitialize backend. It is better to keep
	 * current data rather than log new and risk of failure.
	 */
	log_backend_deactivate(backend);
}

static void dropped(const struct log_backend *const backend, uint32_t cnt)
{
	ARG_UNUSED(backend);

	if (IS_ENABLED(CONFIG_LOG_BACKEND_FS_OUTPUT_DICTIONARY)) {
		log_dict_output_dropped_process(&log_output, cnt);
	} else {
		log_backend_std_dropped(&log_output, cnt);
	}
}

static void process(const struct log_backend *const backend,
		union log_msg_generic *msg)
{
	uint32_t flags = log_backend_std_get_flags();

	log_format_func_t log_output_func = log_format_func_t_get(log_format_current);

	log_output_func(&log_output, &msg->log, flags);
}

static int format_set(const struct log_backend *const backend, uint32_t log_type)
{
	log_format_current = log_type;
	return 0;
}

static const struct log_backend_api log_backend_fs_api = {
	.process = process,
	.panic = panic,
	.init = log_backend_fs_init,
	.dropped = dropped,
	.format_set = format_set,
};

LOG_BACKEND_DEFINE(log_backend_fs, log_backend_fs_api,
		   IS_ENABLED(CONFIG_LOG_BACKEND_FS_AUTOSTART));
#endif
