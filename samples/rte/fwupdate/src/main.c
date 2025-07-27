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

static bool get_filecontent(const char *file, void **buf, size_t *size);
static bool user_has_fw_update_job(size_t *firmware_size);
static int user_deal_with_fw_update_job(size_t firmware_size);

int main(void)
{
	size_t firmware_size = 0;

	mp.mnt_point = DISK_MOUNT_PT;

	int rc = fs_mount(&mp);

	if (rc == FS_RET_OK) {
		LOG_INF("Disk mounted.");
	} else {
		LOG_ERR("Error mounting disk.");
	}

	if (user_has_fw_update_job(&firmware_size)) {
		rc = user_deal_with_fw_update_job(firmware_size);
		LOG_INF("Firmware update job completed with rc=%d", rc);
	}

	return 0;
}

static bool user_has_fw_update_job(size_t *firmware_size)
{
	struct fs_file_t job_file;
	char buf[16];
	ssize_t bytes_read;
	int ret = 0;

	/* Initialize file object */
	fs_file_t_init(&job_file);

#if CONFIG_FWUPDATE_JOBFILE
	/* Try to open job.txt */
	ret = fs_open(&job_file, DISK_MOUNT_PT "/job.txt", FS_O_READ);

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
#endif

	/* Check if fw.elf exists */
	struct fs_dirent dirent;

	ret = fs_stat(DISK_MOUNT_PT "/fw.elf", &dirent);
	if (ret != 0 || dirent.type != FS_DIR_ENTRY_FILE) {
		LOG_WRN("fw.elf not found or not a regular file (rc=%d, errno=%d, type=%d)", ret,
			errno, dirent.type);
		return false;
	}

	LOG_INF("Found fw.elf file (size: %u bytes)", dirent.size);
	*firmware_size = dirent.size;
	return true;
}

static int user_deal_with_fw_update_job(size_t firmware_size)
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

	fs_file_t_init(&fw_file);
	rc = fs_open(&fw_file, DISK_MOUNT_PT "/fw.elf", FS_O_READ);
	if (rc != 0) {
		LOG_ERR("Failed to open firmware file (rc=%d)", rc);
		return rc;
	}

	rc = fw_start(secondary_slot, true, &stream);
	if (rc != 0) {
		LOG_ERR("Failed to start firmware update (rc=%d)", rc);
		return rc;
	}
	/* Write firmware to flash and collect data */
	static uint8_t buf[256];
	size_t total_read = 0;
	ssize_t bytes_read = 0;

	while (total_read < firmware_size) {
		size_t to_read = MIN(sizeof(buf), firmware_size - total_read);

		bytes_read = fs_read(&fw_file, buf, to_read);

		if (bytes_read <= 0) {
			LOG_ERR("Failed to read firmware file (rc=%d)", bytes_read);
			break;
		}

		LOG_DBG("Write %d bytes to flash, percentage: %d%%", bytes_read,
			(total_read + bytes_read) * 100 / firmware_size);

		rc = stream_flash_buffered_write(stream, buf, bytes_read, true);
		if (rc != 0) {
			LOG_ERR("Failed to write firmware to flash (rc=%d)", rc);
			break;
		}

		total_read += bytes_read;
	}

	/* Free the firmware buffer */
	fs_close(&fw_file);

	if (bytes_read <= 0 || rc != 0) {
		LOG_ERR("Firmware update failed, rc=%d", rc);
		return rc;
	}

	/* Try to read and parse hash file for algorithm */
	const char *hash_content = NULL;
	size_t hash_size = 0;

	if (!get_filecontent(DISK_MOUNT_PT "/hash.txt", (void **)&hash_content, &hash_size)) {
		LOG_WRN("hash.txt not found or empty");
	} else {
		LOG_INF("Found hash.txt, size: %zu bytes", hash_size);
	}

	/* Try to read and parse signature file */
	const char *signature_content = NULL;
	size_t signature_size = 0;

	if (!get_filecontent(DISK_MOUNT_PT "/signature.txt", (void **)&signature_content,
			     &signature_size)) {
		LOG_WRN("signature.txt not found or empty");
	} else {
		LOG_INF("Found signature.txt, size: %zu bytes", signature_size);
	}

	/* Try to read and parse trust chain file */
	const char *trust_chain_content = NULL;
	size_t trust_chain_size = 0;

	if (!get_filecontent(DISK_MOUNT_PT "/trust_chain.txt", (void **)&trust_chain_content,
			     &trust_chain_size)) {
		LOG_WRN("trust_chain.txt not found or empty");
	} else {
		LOG_INF("Found trust_chain.txt, size: %zu bytes", trust_chain_size);
	}

	/* Hash verification will be handled automatically in fw_finish() */
	rc = fw_finish(secondary_slot, "FWUPDATE", 8, "SHA256", hash_content, signature_content,
		       trust_chain_content);

	k_free((void *)hash_content);
	k_free((void *)signature_content);
	k_free((void *)trust_chain_content);

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

#if CONFIG_FWUPDATE_JOBFILE
	/* Clean up files */
	rc = fs_unlink(DISK_MOUNT_PT "/job.txt");
	if (rc != 0) {
		LOG_ERR("Failed to remove job.txt (rc=%d)", rc);
		return rc;
	}
	LOG_INF("job.txt removed");
#endif

	return 0;
}

static bool get_filecontent(const char *file, void **buf, size_t *size)
{
	struct fs_dirent dirent;
	struct fs_file_t file_obj;
	int rc;

	/* get file size */
	rc = fs_stat(file, &dirent);
	if (rc != 0 || dirent.type != FS_DIR_ENTRY_FILE) {
		LOG_ERR("File %s not found or not a regular file (rc=%d, type=%d)", file, rc,
			dirent.type);
		return false;
	}
	if (dirent.size == 0) {
		LOG_WRN("File %s is empty", file);
		return false;
	}
	*size = dirent.size;

	/* Allocate buffer and read file content */
	fs_file_t_init(&file_obj);

	rc = fs_open(&file_obj, file, FS_O_READ);
	if (rc != 0) {
		LOG_ERR("Failed to open file %s (rc=%d)", file, rc);
		return false;
	}

	*buf = k_malloc(*size + 1);
	if (!*buf) {
		LOG_ERR("Failed to allocate memory for file content");
		fs_close(&file_obj);
		return false;
	}

	ssize_t bytes_read = fs_read(&file_obj, *buf, *size);

	if (bytes_read < 0) {
		LOG_ERR("Failed to read file %s (rc=%zd)", file, bytes_read);
		k_free(*buf);
		fs_close(&file_obj);
		return false;
	}

	((char *)(*buf))[bytes_read] = '\0';

	fs_close(&file_obj);
	return true;
}
