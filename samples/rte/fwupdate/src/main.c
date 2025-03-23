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
	char buf[8];
	ssize_t bytes_read;
	bool update_pending = false;

	/* Initialize file object */
	fs_file_t_init(&job_file);

	/* Try to open job.txt */
	int ret = fs_open(&job_file, DISK_MOUNT_PT "/job.txt", FS_O_READ);

	if (ret != 0) {
		LOG_DBG("job.txt not found (err %d)", ret);
		return false;
	}

	/* Read file contents */
	bytes_read = fs_read(&job_file, buf, sizeof(buf));
	if (bytes_read > 0) {
		/* Check if "FWUPDATE" is in the file */
		if (!memcmp(buf, "FWUPDATE", 8)) {
			struct fs_dirent fw_file;

			/* Check if fw.elf exists */
			LOG_INF("Found FWUPDATE directive in job.txt");
			ret = fs_stat(DISK_MOUNT_PT "/fw.elf", &fw_file);
			/* Verify it's a regular file and not directory */
			if (ret == 0 && fw_file.type == FS_DIR_ENTRY_FILE) {
				LOG_INF("Found fw.elf file (size: %u bytes)", fw_file.size);
				update_pending = true;
			} else {
				LOG_WRN("fw.elf not found or not a regular file (rc=%d, errno=%d, "
					"type=%d)",
					ret, errno, fw_file.type);
			}
		} else {
			LOG_DBG("job.txt does not contain FWUPDATE directive");
		}
	} else {
		LOG_WRN("Failed to read job.txt or file is empty");
	}

	/* Close the file */
	fs_close(&job_file);

	return update_pending;
}

static int user_deal_with_fw_update_job(void)
{
	int rc;

	LOG_INF("Pending firmware update found.");

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

	/* detect firmware size */
	size_t fw_total_size;

	rc = fs_seek((struct fs_file_t *)&fw_file, 0, FS_SEEK_END);
	if (rc < 0) {
		LOG_ERR("Failed to seek firmware file (rc=%d)", rc);
		fs_close(&fw_file);
		return rc;
	}
	fw_total_size = (size_t)fs_tell(&fw_file);
	rc = fs_seek(&fw_file, 0, FS_SEEK_SET);
	if (rc < 0) {
		LOG_ERR("Failed to seek firmware file (rc=%d) back", rc);
		fs_close(&fw_file);
		return rc;
	}

	/* Write firmware to flash */
	static uint8_t buf[256];
	ssize_t bytes_read;

	while (true) {
		bytes_read = fs_read(&fw_file, buf, sizeof(buf));
		if (bytes_read < 0) {
			LOG_ERR("Failed to read firmware file (rc=%d)", bytes_read);
			fs_close(&fw_file);
			return bytes_read;
		}

		if (bytes_read == 0) {
			break;
		}

		LOG_DBG("Write %d bytes to flash, percentage: %d%%", bytes_read,
			stream_flash_bytes_written(stream) * 100 / fw_total_size);
		LOG_HEXDUMP_DBG(buf, bytes_read, "Firmware data");
		rc = stream_flash_buffered_write(stream, buf, bytes_read, true);
		if (rc != 0) {
			LOG_ERR("Failed to write firmware to flash (rc=%d)", rc);
			fs_close(&fw_file);
			return rc;
		}
	}

	fs_close(&fw_file);

	rc = fw_finish(secondary_slot, "FWUPDATE", 8);
	if (rc != 0) {
		LOG_ERR("Failed to finish firmware update (rc=%d)", rc);
		return rc;
	}

	LOG_INF("Firmware update completed successfully (slot %d)", secondary_slot);

	rc = fs_unlink(DISK_MOUNT_PT "/job.txt");
	if (rc != 0) {
		LOG_ERR("Failed to remove job.txt (rc=%d)", rc);
		return rc;
	}
	LOG_INF("job.txt removed");

	return 0;
}
