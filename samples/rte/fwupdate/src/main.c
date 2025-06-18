/**
 * @file main.c
 * @brief Main file for the firmware update application
 * @copyright SYSFly Co.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <zephyr/uboot.h>

#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "fwupdate.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#if CONFIG_FAT_FILESYSTEM_ELM
#include <ff.h>
/*
 *  Note the fatfs library is able to mount only strings inside _VOLUME_STRS
 *  in ffconf.h
 */
#define DISK_DRIVE_NAME "SD"
#define DISK_MOUNT_PT   "/" DISK_DRIVE_NAME ":"

static FATFS fat_fs;
/* mounting info */
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
};
#define FS_RET_OK FR_OK
#else
#error "This demo is only work with SD card"
#endif

static bool user_has_fw_update_job(void);
static int user_deal_with_fw_update_job(void);

int main(void)
{
	mp.mnt_point = DISK_MOUNT_PT;

	int rc = fs_mount(&mp);

	if (rc == FS_RET_OK) {
		LOG_INF("Disk mounted.");
	} else {
		LOG_ERR("Error mounting disk.");
	}

	if (user_has_fw_update_job()) {
		rc = user_deal_with_fw_update_job();
		LOG_INF("Firmware update job completed with rc=%d", rc);
	}

	return 0;
}

static bool user_has_fw_update_job(void)
{
	struct fs_file_t job_file;
	char buf[16];
	ssize_t bytes_read;

	/* Initialize file object */
	fs_file_t_init(&job_file);

	/* Try to open job.txt */
	int ret = fs_open(&job_file, DISK_MOUNT_PT "/job.txt", FS_O_READ);

	if (ret != 0) {
		LOG_DBG("job.txt not found (err %d)", ret);
		return false;
	}

	/* Read file contents */
	bytes_read = fs_read(&job_file, buf, sizeof(buf) - 1);
	if (bytes_read <= 0) {
		LOG_WRN("Failed to read job.txt or file is empty");
		fs_close(&job_file);
		return false;
	}

	buf[bytes_read] = '\0';

	/* Check if "FWUPDATE" is in the file */
	if (memcmp(buf, "FWUPDATE", 8) != 0) {
		LOG_DBG("job.txt does not contain FWUPDATE directive");
		fs_close(&job_file);
		return false;
	}

	LOG_INF("Found FWUPDATE directive in job.txt");
	fs_close(&job_file);

	/* Check if fw.elf exists */
	struct fs_dirent fw_file;

	ret = fs_stat(DISK_MOUNT_PT "/fw.elf", &fw_file);
	if (ret != 0 || fw_file.type != FS_DIR_ENTRY_FILE) {
		LOG_WRN("fw.elf not found or not a regular file (rc=%d, errno=%d, type=%d)", ret,
			errno, fw_file.type);
		return false;
	}

	LOG_INF("Found fw.elf file (size: %u bytes)", fw_file.size);
	return true;
}

static int user_deal_with_fw_update_job(void)
{
	int rc;
	uint8_t *firmware_buffer = NULL;
	char hash_algorithm[16];

	LOG_INF("Pending firmware update found.");

	/* Get firmware file size */
	struct fs_dirent fw_file_stat;

	rc = fs_stat(DISK_MOUNT_PT "/fw.elf", &fw_file_stat);
	if (rc != 0) {
		LOG_ERR("Failed to get fw.elf file info (rc=%d)", rc);
		return rc;
	}
	size_t firmware_size = fw_file_stat.size;

	/* Try to read and parse hash file for algorithm */
	struct fs_file_t hash_file;

	fs_file_t_init(&hash_file);

	rc = fs_open(&hash_file, DISK_MOUNT_PT "/hash.txt", FS_O_READ);
	if (rc != 0) {
		LOG_WRN("hash.txt not found (rc=%d)", rc);
		return rc;
	}

	char hash_content[128];
	ssize_t bytes_read = fs_read(&hash_file, hash_content, sizeof(hash_content) - 1);

	fs_close(&hash_file);

	if (bytes_read > 0) {
		hash_content[bytes_read] = '\0';

		/* Parse algorithm from hash.txt (format: ALGORITHM:HASH) */
		const char *colon_pos = strchr(hash_content, ':');

		if (!colon_pos) {
			LOG_WRN("hash.txt does not contain a valid algorithm:hash format");
			return -EINVAL;
		}
		size_t algo_len = colon_pos - hash_content;

		if (algo_len < sizeof(hash_algorithm)) {
			memcpy(hash_algorithm, hash_content, algo_len);
			hash_algorithm[algo_len] = '\0';

			/* Convert to uppercase */
			size_t hash_len = strnlen(hash_algorithm, sizeof(hash_algorithm));

			for (int i = 0; i < hash_len; i++) {
				hash_algorithm[i] = (char)toupper((unsigned char)hash_algorithm[i]);
			}

			LOG_INF("Parsed hash algorithm: %s", hash_algorithm);
		} else {
			LOG_WRN("Parsed hash algorithm failed.");
		}
	}

	/* Perform firmware update */
	rc = fw_init();
	if (rc != 0) {
		LOG_ERR("Failed to initialize firmware update module (rc=%d)", rc);
		return rc;
	}

	uint8_t secondary_slot = fw_is_primary(0) ? 1 : 0;
	struct stream_flash_ctx *stream = NULL;
	struct fs_file_t fw_file;

	rc = fw_start(secondary_slot, true, &stream);
	if (rc != 0) {
		LOG_ERR("Failed to start firmware update (rc=%d)", rc);
		return rc;
	}

	fs_file_t_init(&fw_file);
	rc = fs_open(&fw_file, DISK_MOUNT_PT "/fw.elf", FS_O_READ);
	if (rc != 0) {
		LOG_ERR("Failed to open firmware file (rc=%d)", rc);
		return rc;
	}

	/* Allocate buffer for firmware data */
	firmware_buffer = k_malloc(firmware_size);
	if (!firmware_buffer) {
		LOG_ERR("Failed to allocate memory for firmware buffer");
		fs_close(&fw_file);
		return -ENOMEM;
	}

	/* Write firmware to flash and collect data */
	static uint8_t buf[256];
	size_t total_read = 0;

	while (total_read < firmware_size) {
		size_t to_read = MIN(sizeof(buf), firmware_size - total_read);

		bytes_read = fs_read(&fw_file, buf, to_read);

		if (bytes_read <= 0) {
			LOG_ERR("Failed to read firmware file (rc=%d)", bytes_read);
			k_free(firmware_buffer);
			fs_close(&fw_file);
			return bytes_read;
		}

		/* Copy to buffer */
		memcpy(firmware_buffer + total_read, buf, bytes_read);

		LOG_DBG("Write %d bytes to flash, percentage: %d%%", bytes_read,
			(total_read + bytes_read) * 100 / firmware_size);

		rc = stream_flash_buffered_write(stream, buf, bytes_read, true);
		if (rc != 0) {
			LOG_ERR("Failed to write firmware to flash (rc=%d)", rc);
			k_free(firmware_buffer);
			fs_close(&fw_file);
			return rc;
		}

		total_read += bytes_read;
	}

	fs_close(&fw_file);

	/* Free the firmware buffer */
	k_free(firmware_buffer);

	/* Hash verification will be handled automatically in fw_finish() */
	rc = fw_finish(secondary_slot, "FWUPDATE", 8, hash_algorithm);
	if (rc != 0) {
		LOG_ERR("Failed to finish firmware update (rc=%d)", rc);
		return rc;
	}

	/* Verify the firmware (includes hash verification) */
	if (!fw_is_valid(secondary_slot)) {
		LOG_ERR("Firmware validation failed for slot %d", secondary_slot);
		return -EINVAL;
	}

	LOG_INF("Firmware update and validation completed successfully (slot %d)", secondary_slot);

	/* Clean up files */
	rc = fs_unlink(DISK_MOUNT_PT "/job.txt");
	if (rc != 0) {
		LOG_ERR("Failed to remove job.txt (rc=%d)", rc);
		return rc;
	}
	LOG_INF("job.txt removed");

	return 0;
}
