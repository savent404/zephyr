/*
 * Copyright (c) 2024 SYSTech Co.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/disk.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sd/sd_spec.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys_clock.h>
#include <ff.h>

#include "rte_main.h"

#define LOG_MODULE_NAME main
LOG_MODULE_REGISTER(LOG_MODULE_NAME, LOG_LEVEL_DBG);

static int fs_default_mount(void);

int main(void)
{
	int32_t ret;
	uint32_t sleep_ms = 10;

	LOG_INF("Board name is %s\n", CONFIG_BOARD_TARGET);
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

#if defined(CONFIG_DISK_DRIVER_SDMMC)
#define DISK_NAME CONFIG_SDMMC_VOLUME_NAME
#elif defined(CONFIG_DISK_DRIVER_MMC)
#define DISK_NAME CONFIG_MMC_VOLUME_NAME
#else
#error "No disk device defined, is your board supported?"
#endif

#define LFS0_PARTITION_NODE DT_NODELABEL(lfs0_partition)
#define LFS1_PARTITION_NODE DT_NODELABEL(lfs1_partition)

struct partition_disk {
	struct disk_info info;
	const char *parent_name;
	uint32_t offset;
	uint32_t size;
	uint32_t start_sector;
	uint32_t sector_count;
	uint32_t sector_size;
};

static struct partition_disk *partition_from_disk(struct disk_info *disk)
{
	return CONTAINER_OF(disk, struct partition_disk, info);
}

static int partition_setup(struct partition_disk *part)
{
	uint32_t parent_sector_count;
	uint64_t parent_size;
	uint64_t end;
	int ret;

	if (part->sector_size != 0U) {
		return 0;
	}

	ret = disk_access_init(part->parent_name);
	if (ret < 0) {
		return ret;
	}

	ret = disk_access_ioctl(part->parent_name, DISK_IOCTL_GET_SECTOR_SIZE, &part->sector_size);
	if (ret < 0) {
		return ret;
	}

	ret = disk_access_ioctl(part->parent_name, DISK_IOCTL_GET_SECTOR_COUNT,
			       &parent_sector_count);
	if (ret < 0) {
		return ret;
	}

	if ((part->offset % part->sector_size) != 0U || (part->size % part->sector_size) != 0U) {
		return -EINVAL;
	}

	end = (uint64_t)part->offset + part->size;
	parent_size = (uint64_t)parent_sector_count * part->sector_size;
	if (end < part->offset || end > parent_size) {
		return -EINVAL;
	}

	part->start_sector = part->offset / part->sector_size;
	part->sector_count = part->size / part->sector_size;

	return 0;
}

static int partition_disk_init(struct disk_info *disk)
{
	return partition_setup(partition_from_disk(disk));
}

static int partition_disk_status(struct disk_info *disk)
{
	struct partition_disk *part = partition_from_disk(disk);

	return disk_access_status(part->parent_name);
}

static int partition_disk_read(struct disk_info *disk, uint8_t *data_buf, uint32_t start_sector,
			       uint32_t num_sector)
{
	struct partition_disk *part = partition_from_disk(disk);
	uint32_t end_sector = start_sector + num_sector;
	int ret;

	ret = partition_setup(part);
	if (ret < 0) {
		return ret;
	}

	if (end_sector < start_sector || end_sector > part->sector_count) {
		return -EIO;
	}

	return disk_access_read(part->parent_name, data_buf, part->start_sector + start_sector,
				num_sector);
}

static int partition_disk_write(struct disk_info *disk, const uint8_t *data_buf,
				uint32_t start_sector, uint32_t num_sector)
{
	struct partition_disk *part = partition_from_disk(disk);
	uint32_t end_sector = start_sector + num_sector;
	int ret;

	ret = partition_setup(part);
	if (ret < 0) {
		return ret;
	}

	if (end_sector < start_sector || end_sector > part->sector_count) {
		return -EIO;
	}

	return disk_access_write(part->parent_name, data_buf, part->start_sector + start_sector,
				 num_sector);
}

static int partition_disk_ioctl(struct disk_info *disk, uint8_t cmd, void *buff)
{
	struct partition_disk *part = partition_from_disk(disk);
	int ret;

	switch (cmd) {
	case DISK_IOCTL_CTRL_INIT:
		return partition_setup(part);
	case DISK_IOCTL_GET_SECTOR_COUNT:
		ret = partition_setup(part);
		if (ret < 0) {
			return ret;
		}
		*(uint32_t *)buff = part->sector_count;
		return 0;
	case DISK_IOCTL_GET_SECTOR_SIZE:
		ret = partition_setup(part);
		if (ret < 0) {
			return ret;
		}
		*(uint32_t *)buff = part->sector_size;
		return 0;
	case DISK_IOCTL_GET_ERASE_BLOCK_SZ:
		*(uint32_t *)buff = 1U;
		return 0;
	case DISK_IOCTL_CTRL_SYNC:
		return disk_access_ioctl(part->parent_name, cmd, buff);
	case DISK_IOCTL_CTRL_DEINIT:
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct disk_operations partition_disk_ops = {
	.init = partition_disk_init,
	.status = partition_disk_status,
	.read = partition_disk_read,
	.write = partition_disk_write,
	.ioctl = partition_disk_ioctl,
};

static struct partition_disk lfs0_disk = {
	.info.name = "LFS0",
	.info.ops = &partition_disk_ops,
	.parent_name = DISK_NAME,
	.offset = DT_REG_ADDR(LFS0_PARTITION_NODE),
	.size = DT_REG_SIZE(LFS0_PARTITION_NODE),
};

static struct partition_disk lfs1_disk = {
	.info.name = "LFS1",
	.info.ops = &partition_disk_ops,
	.parent_name = DISK_NAME,
	.offset = DT_REG_ADDR(LFS1_PARTITION_NODE),
	.size = DT_REG_SIZE(LFS1_PARTITION_NODE),
};

FS_LITTLEFS_DECLARE_CUSTOM_CONFIG(lfs0_data, CONFIG_SDHC_BUFFER_ALIGNMENT, SDMMC_DEFAULT_BLOCK_SIZE,
				  SDMMC_DEFAULT_BLOCK_SIZE, SDMMC_DEFAULT_BLOCK_SIZE,
				  2 * SDMMC_DEFAULT_BLOCK_SIZE);
FS_LITTLEFS_DECLARE_CUSTOM_CONFIG(lfs1_data, CONFIG_SDHC_BUFFER_ALIGNMENT, SDMMC_DEFAULT_BLOCK_SIZE,
				  SDMMC_DEFAULT_BLOCK_SIZE, SDMMC_DEFAULT_BLOCK_SIZE,
				  2 * SDMMC_DEFAULT_BLOCK_SIZE);

static struct fs_mount_t lfs0_mount = {
	.type = FS_LITTLEFS,
	.fs_data = &lfs0_data,
	.flags = FS_MOUNT_FLAG_USE_DISK_ACCESS,
	.storage_dev = "LFS0",
	.mnt_point = "/lfs",
};

static struct fs_mount_t lfs1_mount = {
	.type = FS_LITTLEFS,
	.fs_data = &lfs1_data,
	.flags = FS_MOUNT_FLAG_USE_DISK_ACCESS,
	.storage_dev = "LFS1",
	.mnt_point = "/lfs1",
};

static int fs_default_mount(void)
{
	int ret;

	ret = disk_access_register(&lfs0_disk.info);
	if (ret < 0) {
		LOG_ERR("Failed to register LFS0 disk: %d", ret);
		return ret;
	}

	ret = disk_access_register(&lfs1_disk.info);
	if (ret < 0) {
		LOG_ERR("Failed to register LFS1 disk: %d", ret);
		return ret;
	}

	ret = fs_mount(&lfs0_mount);
	if (ret < 0) {
		LOG_ERR("FAIL: mount %s at %s: %d", (const char *)lfs0_mount.storage_dev,
			lfs0_mount.mnt_point, ret);
		return ret;
	}

	LOG_INF("%s mount: %d", lfs0_mount.mnt_point, ret);

	ret = fs_mount(&lfs1_mount);
	if (ret < 0) {
		LOG_ERR("FAIL: mount %s at %s: %d", (const char *)lfs1_mount.storage_dev,
			lfs1_mount.mnt_point, ret);
		(void)fs_unmount(&lfs0_mount);
		return ret;
	}

	LOG_INF("%s mount: %d", lfs1_mount.mnt_point, ret);
	return 0;
}
