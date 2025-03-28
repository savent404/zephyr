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

#define CHUNK_SIZE (8192)

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
#define DST_FILE_PATH SD_MOUNT_PT "/" SOME_FILE_NAME

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

/**
 * @brief Transfer file from source to destination.
 *
 * This function copies the contents of the source file to the destination file by
 * reading and writing data in chunks. The buffer size is dynamically allocated based
 * on the file size (using a maximum block defined by CHUNK_SIZE).
 *
 * @param src_path Source file absolute path.
 * @param dst_path Destination file absolute path.
 * @return 0 on success, negative errno on failure.
 */
static int file_sync_transfer(const char *src_path, const char *dst_path)
{
	struct fs_file_t src_file;
	struct fs_file_t dst_file;
	int rc = 0;
	struct fs_dirent entry;
	size_t total_size = 0;
	size_t remaining = 0;
	int chunk_size = 0;
	int read_size = 0;
	uint8_t *buffer = NULL;
	size_t buf_size = CHUNK_SIZE;

	if (!src_path || !dst_path) {
		return -EINVAL;
	}

	rc = fs_stat(src_path, &entry);
	if (rc != 0 || entry.type != FS_DIR_ENTRY_FILE) {
		LOG_ERR("Source file %s not found or is not a file", src_path);
		return -ENOENT;
	}
	total_size = entry.size;
	LOG_INF("File size: %zu bytes", total_size);
	LOG_INF("Transferring from %s to %s", src_path, dst_path);

	buf_size = (total_size < CHUNK_SIZE) ? total_size : CHUNK_SIZE;
	buffer = k_malloc(buf_size);
	if (!buffer) {
		LOG_ERR("Failed to allocate buffer of size %zu", buf_size);
		return -ENOMEM;
	}

	fs_file_t_init(&src_file);
	fs_file_t_init(&dst_file);

	rc = fs_open(&src_file, src_path, FS_O_READ);
	if (rc != 0) {
		LOG_ERR("Failed to open source file %s", src_path);
		goto out;
	}

	rc = fs_open(&dst_file, dst_path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (rc != 0) {
		LOG_ERR("Failed to open destination file %s", dst_path);
		goto out;
	}

	remaining = total_size;
	while (remaining > 0) {
		chunk_size = MIN(remaining, CHUNK_SIZE);

		read_size = fs_read(&src_file, buffer, chunk_size);
		if (read_size < 0) {
			LOG_ERR("Read error: %d", read_size);
			goto out;
		}
		if (read_size == 0) {
			LOG_WRN("Unexpected EOF while reading source file");
			break;
		}

		rc = fs_write(&dst_file, buffer, read_size);
		if (rc < 0) {
			LOG_ERR("Write error: %d", rc);
			goto out;
		}
		if (rc != read_size) {
			LOG_ERR("Write incomplete: %d of %d bytes", rc, read_size);
			goto out;
		}

		remaining -= read_size;
	}

	rc = fs_sync(&dst_file);
	if (rc != 0) {
		LOG_ERR("Failed to sync destination file: %d", rc);
		goto out;
	}

	goto out;

out:
	if (fs_close(&src_file) != 0) {
		LOG_WRN("Failed to close source file");
	}
	if (fs_close(&dst_file) != 0) {
		LOG_WRN("Failed to close destination file");
	}
	k_free(buffer);
	return rc;
}

int main(void)
{
	k_msleep(1000);

	int rc;

	LOG_INF("Sample program to r/w files on littlefs\n");

	rc = fs_mount(&mp_emmc);
	if (rc != FS_RET_OK) {
		LOG_ERR("MMC mount failed: %d", rc);
		return -1;
	}

	lsdir(mp_emmc.mnt_point);

	k_msleep(1000);

	rc = fs_mount(&mp_sd);
	if (rc != FS_RET_OK) {
		LOG_ERR("SD mount failed: %d", rc);
		return -1;
	}

	LOG_INF("SD card detecting: ABC1.DAT\n");

	if (fs_unlink("/SD:/ABC1.DAT") != 0) {
		LOG_INF("Delete file failed");
	}
	lsdir(mp_sd.mnt_point);

	if (file_sync_transfer(SRC_FILE_PATH, DST_FILE_PATH) != 0) {
		LOG_ERR("File sync transfer failed");
		return -1;
	}

	lsdir(mp_sd.mnt_point);
	return 0;
}
