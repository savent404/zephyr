/*
 * Copyright (c) 2026 SYSTech Co.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_backend_fs.h>
#include <zephyr/sd/sd_spec.h>

LOG_MODULE_REGISTER(log_fs_downgrade, LOG_LEVEL_DBG);

#define STATUS_PRINT_INTERVAL_MS 5000
#define LOG_INTERVAL_MS          100

static int fs_default_mount(void);

FS_LITTLEFS_DECLARE_CUSTOM_CONFIG(lfs_data, CONFIG_SDHC_BUFFER_ALIGNMENT, SDMMC_DEFAULT_BLOCK_SIZE,
				  SDMMC_DEFAULT_BLOCK_SIZE, SDMMC_DEFAULT_BLOCK_SIZE,
				  2 * SDMMC_DEFAULT_BLOCK_SIZE);

#if defined(CONFIG_DISK_DRIVER_SDMMC)
#define DISK_NAME CONFIG_SDMMC_VOLUME_NAME
#elif defined(CONFIG_DISK_DRIVER_MMC)
#define DISK_NAME CONFIG_MMC_VOLUME_NAME
#else
#error "No disk device defined, is your board supported?"
#endif

static struct fs_mount_t littlefs_mnt = {
	.type = FS_LITTLEFS,
	.fs_data = &lfs_data,
	.flags = FS_MOUNT_FLAG_USE_DISK_ACCESS,
	.storage_dev = DISK_NAME,
	.mnt_point = CONFIG_LOG_BACKEND_FS_DIR,
};

int main(void)
{
	uint32_t counter = 0;
	int64_t next_status = k_uptime_get();
	int rc;

	printk("log_fs_downgrade: board=%s dir=%s file_size=%d files=%d threshold=%d\n",
	       CONFIG_BOARD_TARGET, CONFIG_LOG_BACKEND_FS_DIR, CONFIG_LOG_BACKEND_FS_FILE_SIZE,
	       CONFIG_LOG_BACKEND_FS_FILES_LIMIT, CONFIG_LOG_BACKEND_FS_DEGRADE_FAILURE_THRESHOLD);

	rc = fs_default_mount();
	if (rc < 0) {
		printk("log_fs_downgrade: mount failed rc=%d\n", rc);
		return rc;
	}

	while (true) {
		LOG_INF("log_fs_downgrade counter=%u payload=%08x%08x%08x%08x", counter, counter,
			counter + 1, counter + 2, counter + 3);

		if (k_uptime_get() >= next_status) {
			printk("log_fs_downgrade: degraded=%d failures=%u last_error=%d\n",
			       log_backend_fs_is_degraded(), log_backend_fs_failure_count_get(),
			       log_backend_fs_last_error_get());
			next_status += STATUS_PRINT_INTERVAL_MS;
		}

		counter++;
		k_msleep(LOG_INTERVAL_MS);
	}

	return 0;
}

static int fs_default_mount(void)
{
	int rc;

	rc = fs_mount(&littlefs_mnt);
	if (rc < 0) {
		LOG_ERR("FAIL: mount id %" PRIuPTR " at %s: %d",
			(uintptr_t)(littlefs_mnt.storage_dev), littlefs_mnt.mnt_point, rc);
		return rc;
	}

	LOG_INF("%s mount: %d", littlefs_mnt.mnt_point, rc);
	return 0;
}
