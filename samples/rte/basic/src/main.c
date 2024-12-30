/*
 * Copyright (c) 2024 SYSTech Co.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include <stdio.h>
#include <zephyr/sys_clock.h>
#include <zephyr/sd/sd_spec.h>
#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <ff.h>

#include "rte_main.h"

#define LOG_MODULE_NAME main
LOG_MODULE_REGISTER(LOG_MODULE_NAME, LOG_LEVEL_DBG);

static int fs_default_mount(void);

int main(void)
{
	int32_t ret;
	uint32_t sleep_ms = 10;

	printf("Board name is %s\n", CONFIG_BOARD_TARGET);
	if (fs_default_mount()) {
		return -1;
	}

	ret = RteInit();
	if (ret != 0) {
		LOG_ERR("RteInit err");
		return ret;
	}

	while (1) {
		k_msleep(sleep_ms);
	}
	return 0;
}

FS_LITTLEFS_DECLARE_CUSTOM_CONFIG(lfs_data, CONFIG_SDHC_BUFFER_ALIGNMENT, SDMMC_DEFAULT_BLOCK_SIZE,
				  SDMMC_DEFAULT_BLOCK_SIZE, SDMMC_DEFAULT_BLOCK_SIZE,
				  2 * SDMMC_DEFAULT_BLOCK_SIZE);

static struct fs_mount_t littlefs_mnt = {
	.type = FS_LITTLEFS,
	.fs_data = &lfs_data,
	.flags = FS_MOUNT_FLAG_USE_DISK_ACCESS,
	.storage_dev = "MMC",
};
static struct fs_mount_t *lfs_mpt = &littlefs_mnt;

static int fs_default_mount(void)
{

	return 0;
	/* FIXME: This is a workaround to avoid the issue that the mount point is not created */
	int32_t rc;

	lfs_mpt->mnt_point = "/lfs";
	rc = fs_mount(lfs_mpt);
	if (rc < 0) {
		printk("FAIL: mount id %" PRIuPTR " at %s: %d\n", (uintptr_t)(lfs_mpt->storage_dev),
		       lfs_mpt->mnt_point, rc);
		return rc;
	}
	printk("%s mount: %d\n", lfs_mpt->mnt_point, rc);
}
