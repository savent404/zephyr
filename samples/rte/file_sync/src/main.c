/**
 * @file main.c
 * @brief
 * @copyright SYSFly Co.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>
#include <ff.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

#define CHUNK_SIZE (512)

#ifdef CONFIG_FAT_FILESYSTEM_ELM
#define FS_RET_OK FR_OK
#else
#define FS_RET_OK 0
#endif

#define MAX_PATH          128
#define SOME_FILE_NAME    "abc1.dat"
#define SOME_DIR_NAME     "abc1"
#define SOME_REQUIRED_LEN MAX(sizeof(SOME_FILE_NAME), sizeof(SOME_DIR_NAME))

#define SD_DRIVE_NAME "SD"
#define SD_MOUNT_PT   "/" SD_DRIVE_NAME ":"

static FATFS fat_fs_sd;
static struct fs_mount_t mp_sd = {
	.type = FS_FATFS,
	.fs_data = &fat_fs_sd,
	.mnt_point = SD_MOUNT_PT,
};

#if defined(CONFIG_DISK_DRIVER_MMC)
#define EMMC_DRIVE_NAME CONFIG_MMC_VOLUME_NAME
#define EMMC_MOUNT_PT   "/" EMMC_DRIVE_NAME ":"
#else
#error "No disk device defined, is your board supported?"
#endif

struct fs_littlefs lfsfs_emmc;
static struct fs_mount_t mp_emmc = {
	.type = FS_LITTLEFS,
	.fs_data = &lfsfs_emmc,
	.storage_dev = (void *)EMMC_DRIVE_NAME,
	.flags = FS_MOUNT_FLAG_USE_DISK_ACCESS,
	.mnt_point = EMMC_MOUNT_PT,
};
struct fs_mount_t *mountpoint = &mp_emmc;

#define SRC_FILE_PATH EMMC_MOUNT_PT "/" SOME_FILE_NAME
#define DST_FILE_PATH                                                                              \
	SD_MOUNT_PT "/"                                                                            \
		    "ABC1.DAT"

/* List dir entry by path
 *
 * @param path Absolute path to list
 *
 * @return Negative errno code on error, number of listed entries on
 *         success.
 */
static int lsdir(const char *path)
{
	int res;
	struct fs_dir_t dirp;
	static struct fs_dirent entry;
	int count = 0;

	fs_dir_t_init(&dirp);

	res = fs_opendir(&dirp, path);
	if (res) {
		LOG_ERR("Error opening dir %s [%d]", path, res);
		return res;
	}

	LOG_INF("\n ===============Listing dir %s===============\n", path);
	for (;;) {
		res = fs_readdir(&dirp, &entry);

		if (res || entry.name[0] == 0) {
			if (res < 0) {
				LOG_ERR("Error reading dir [%d]\n", res);
			}
			break;
		}

		if (entry.type == FS_DIR_ENTRY_DIR) {
			LOG_INF("[DIR ] %s\n", entry.name);
		} else {
			LOG_INF("[FILE] %s (size = %zu)\n", entry.name, entry.size);
		}
		count++;
	}

	fs_closedir(&dirp);
	if (res == 0) {
		res = count;
	}

	return res;
}

static bool create_abc1_entries(const char *base_path)
{
	char path[MAX_PATH];
	struct fs_file_t file;
	int base = strlen(base_path);

	fs_file_t_init(&file);

	if (base >= (sizeof(path) - SOME_REQUIRED_LEN)) {
		LOG_ERR("Not enough concatenation buffer to create file paths");
		return false;
	}

	/* Copy base path and append "/" and file name */
	strncpy(path, base_path, sizeof(path));

	path[base++] = '/';
	path[base] = 0;
	strcat(&path[base], SOME_FILE_NAME);

	/* Open file for create and write */
	if (fs_open(&file, path, FS_O_CREATE | FS_O_WRITE) != 0) {
		LOG_ERR("Failed to create file %s", path);
		return false;
	}

	uint8_t test_data[512] = {0};

	/* Prepare test data */
	for (int i = 0; i < sizeof(test_data); i++) {
		test_data[i] = i % 256;
	}

	/* Write test data to file */
	if (fs_write(&file, test_data, sizeof(test_data)) != sizeof(test_data)) {
		LOG_ERR("Write test data failed");
		fs_close(&file);
		return false;
	}

	/* Sync file to ensure data is written */
	if (fs_sync(&file) != 0) {
		LOG_WRN("File sync failed");
	}

	fs_close(&file);

	return true;
}

/**
 * @brief Transfer file from source to destination with verification
 *
 * Copies file contents between storage devices in chunks, performs
 * byte-to-byte comparison to ensure data integrity.
 *
 * @param src_path Absolute path of source file (must exist)
 * @param dst_path Absolute path of destination file (will be overwritten)
 * @retval 0 Transfer successful with content verified
 * @retval -EINVAL Invalid input parameters
 * @retval -ENOENT Source file not found
 * @retval -ENOMEM Memory allocation failure
 * @retval -EIO Data verification failed
 */
static int file_sync_transfer(const char *src_path, const char *dst_path)
{
	struct fs_file_t src_file, dst_file, src_cmp, dst_cmp;
	int rc = 0;
	struct fs_dirent entry;
	size_t total_size = 0;
	size_t remaining = 0;
	int chunk_size = 0;
	int read_size = 0;
	uint8_t *buffer = NULL;
	uint8_t *src_buffer = NULL;
	uint8_t *dst_buffer = NULL;
	size_t buf_size = CHUNK_SIZE;
	bool match = true;

	if (!src_path || !dst_path) {
		return -EINVAL;
	}

	/* Get source file information to validate existence and obtain file size */
	rc = fs_stat(src_path, &entry);
	if (rc != 0 || entry.type != FS_DIR_ENTRY_FILE) {
		LOG_ERR("Source file %s not found or is not a file", src_path);
		return -ENOENT;
	}
	total_size = entry.size;
	LOG_INF("File size: %zu bytes", total_size);
	LOG_INF("Transferring from %s to %s", src_path, dst_path);

	/* Allocate buffer size based on file size and pre-defined chunk size */
	buf_size = (total_size < CHUNK_SIZE) ? total_size : CHUNK_SIZE;
	buffer = k_malloc(buf_size);
	if (!buffer) {
		LOG_ERR("Failed to allocate buffer of size %zu", buf_size);
		return -ENOMEM;
	}

	fs_file_t_init(&src_file);
	fs_file_t_init(&dst_file);

	/* Open source file for reading */
	rc = fs_open(&src_file, src_path, FS_O_READ);
	if (rc != 0) {
		LOG_ERR("Failed to open source file %s", src_path);
		goto cleanup;
	}

	/* Open destination file for writing; create or truncate if exists */
	rc = fs_open(&dst_file, dst_path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (rc != 0) {
		LOG_ERR("Failed to open destination file %s", dst_path);
		goto cleanup;
	}

	remaining = total_size;
	while (remaining > 0) {
		chunk_size = MIN(remaining, CHUNK_SIZE);

		/* Read from source file */
		read_size = fs_read(&src_file, buffer, chunk_size);
		if (read_size < 0) {
			LOG_ERR("Read error: %d", read_size);
			goto cleanup;
		}
		if (read_size == 0) {
			LOG_WRN("Unexpected EOF while reading source file");
			break;
		}

		/* Write to destination file */
		rc = fs_write(&dst_file, buffer, read_size);
		if (rc < 0) {
			LOG_ERR("Write error: %d", rc);
			goto cleanup;
		}
		if (rc != read_size) {
			LOG_ERR("Write incomplete: %d of %d bytes", rc, read_size);
			goto cleanup;
		}

		remaining -= read_size;
	}

	/* ync destination file to ensure data is flushed */
	rc = fs_sync(&dst_file);
	if (rc != 0) {
		LOG_ERR("Failed to sync destination file: %d", rc);
		goto cleanup;
	}
	fs_close(&src_file);
	fs_close(&dst_file);

	/* Reopen source and destination files for data verification */
	fs_file_t_init(&src_cmp);
	fs_file_t_init(&dst_cmp);
	rc = fs_open(&src_cmp, src_path, FS_O_READ);
	if (rc != 0) {
		LOG_ERR("Failed to reopen source file for comparison");
		goto cleanup;
	}

	rc = fs_open(&dst_cmp, dst_path, FS_O_READ);
	if (rc != 0) {
		LOG_ERR("Failed to reopen destination file for comparison");
		fs_close(&src_cmp);
		goto cleanup;
	}

	/* Allocate buffers for comparing file data */
	*src_buffer = k_malloc(CHUNK_SIZE);
	*dst_buffer = k_malloc(CHUNK_SIZE);

	if (!src_buffer || !dst_buffer) {
		LOG_ERR("Failed to allocate compare buffers");
		rc = -ENOMEM;
		goto cleanup_cmp;
	}

	size_t pos = 0;

	while (pos < total_size) {
		chunk_size = MIN(total_size - pos, CHUNK_SIZE);

		/* Read chunk from source file */
		read_size = fs_read(&src_cmp, src_buffer, chunk_size);
		if (read_size <= 0 || (size_t)read_size != chunk_size) {
			LOG_ERR("Source read error: expect=%d actual=%d", chunk_size, read_size);
			match = false;

			break;
		}

		/* Read chunk from destination file */
		int dst_read = fs_read(&dst_cmp, dst_buffer, chunk_size);

		if (dst_read <= 0 || (size_t)dst_read != chunk_size) {
			LOG_ERR("Dest read error: expect=%d actual=%d", chunk_size, dst_read);
			match = false;
			break;
		}

		/* Compare the two chunks byte-to-byte */
		if (memcmp(src_buffer, dst_buffer, chunk_size) != 0) {
			LOG_ERR("Content mismatch at offset %zu", pos);
			match = false;
			break;
		}

		LOG_INF("Content match at offset %zu", pos);

		pos += chunk_size;
	}

	fs_close(&src_cmp);
	fs_close(&dst_cmp);

	if (!match) {
		rc = -EIO;
	} else {
		LOG_INF("Sync success: %zu bytes verified [%s] -> [%s]", total_size, src_path,
			dst_path);
	}
	goto cleanup;

cleanup_cmp:
	fs_close(&src_cmp);
	fs_close(&dst_cmp);

cleanup:
	fs_close(&src_file);
	fs_close(&dst_file);
	k_free(buffer);
	k_free(src_buffer);
	k_free(dst_buffer);
	return rc;
}

int main(void)
{
	k_msleep(1000);

	int rc;

	LOG_INF("Sample program to r/w files on littlefs\n");

	/* Mount the eMMC filesystem */
	rc = fs_mount(&mp_emmc);
	if (rc != FS_RET_OK) {
		LOG_ERR("MMC mount failed: %d", rc);
		return -1;
	}

	/* Delete source file if it exists (ignore error if not present) */
	if (fs_unlink(SRC_FILE_PATH) != 0) {
		LOG_INF("EMMC file not exist or delete failed");
	}
	LOG_INF("Creating abc1.dat in %s", mp_emmc.mnt_point);
	if (create_abc1_entries(mp_emmc.mnt_point)) {
		lsdir(mp_emmc.mnt_point);
	}

	k_msleep(1000);

	/* Mount the SD filesystem */
	rc = fs_mount(&mp_sd);
	if (rc != FS_RET_OK) {
		LOG_ERR("SD mount failed: %d", rc);
		return -1;
	}

	LOG_INF("SD card detecting: ABC1.DAT\n");

	/* Delete destination file if it exists (ignore error if not present) */
	if (fs_unlink(DST_FILE_PATH) != 0) {
		LOG_INF("SD file not exist or delete failed");
	}
	lsdir(mp_sd.mnt_point);

	/* Transfer file from source to destination and verify */
	if (file_sync_transfer(SRC_FILE_PATH, DST_FILE_PATH) != 0) {
		LOG_ERR("File sync transfer failed");
		return -1;
	}

	lsdir(mp_sd.mnt_point);
	return 0;
}
