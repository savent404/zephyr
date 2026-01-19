/*
 * Copyright (c) 2026 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "disk_device.h"
#include <zephyr/storage/disk_access.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <errno.h>

LOG_MODULE_REGISTER(disk_emmc, CONFIG_DISK_TOOLS_LOG_LEVEL);

/* eMMC device configuration */
#define EMMC_DEVICE_NAME "MMC"
#define EMMC_SECTOR_SIZE 512

static struct {
	bool initialized;
	uint32_t sector_size;
	uint32_t sector_count;
} emmc_ctx;

/**
 * @brief Initialize eMMC device
 */
static int emmc_init(void)
{
	int ret;

	if (emmc_ctx.initialized) {
		return 0;
	}

	/* Initialize disk subsystem */
	ret = disk_access_init(EMMC_DEVICE_NAME);
	if (ret != 0) {
		LOG_ERR("Failed to initialize disk access: %d", ret);
		return ret;
	}

	/* Get sector size */
	ret = disk_access_ioctl(EMMC_DEVICE_NAME, DISK_IOCTL_GET_SECTOR_SIZE,
				&emmc_ctx.sector_size);
	if (ret != 0) {
		LOG_ERR("Failed to get sector size: %d", ret);
		return ret;
	}

	/* Get sector count */
	ret = disk_access_ioctl(EMMC_DEVICE_NAME, DISK_IOCTL_GET_SECTOR_COUNT,
				&emmc_ctx.sector_count);
	if (ret != 0) {
		LOG_ERR("Failed to get sector count: %d", ret);
		return ret;
	}

	LOG_INF("eMMC initialized: sector_size=%u, sector_count=%u", emmc_ctx.sector_size,
		emmc_ctx.sector_count);

	emmc_ctx.initialized = true;
	return 0;
}

/**
 * @brief Read data from eMMC
 */
static int emmc_read(size_t offset, uint8_t *buf, size_t len)
{
	int ret;
	uint32_t start_sector;
	uint32_t sector_count;
	size_t start_offset;
	size_t end_offset;
	size_t copy_offset;
	size_t copy_len;
	uint8_t sector_buf[EMMC_SECTOR_SIZE];

	if (!emmc_ctx.initialized) {
		ret = emmc_init();
		if (ret != 0) {
			return ret;
		}
	}

	if (len == 0) {
		return 0;
	}

	/* Calculate sector-aligned boundaries */
	start_sector = offset / emmc_ctx.sector_size;
	start_offset = offset % emmc_ctx.sector_size;
	end_offset = (offset + len + emmc_ctx.sector_size - 1) / emmc_ctx.sector_size;
	sector_count = end_offset - start_sector;

	/* Check boundaries */
	if (start_sector + sector_count > emmc_ctx.sector_count) {
		LOG_ERR("Read beyond device boundary");
		return -EINVAL;
	}

	copy_offset = 0;

	/* Read first partial sector if not aligned */
	if (start_offset != 0) {
		ret = disk_access_read(EMMC_DEVICE_NAME, sector_buf, start_sector, 1);
		if (ret != 0) {
			LOG_ERR("Failed to read sector %u: %d", start_sector, ret);
			return ret;
		}

		copy_len = MIN(emmc_ctx.sector_size - start_offset, len);
		memcpy(buf, sector_buf + start_offset, copy_len);
		copy_offset += copy_len;
		start_sector++;
		sector_count--;
	}

	/* Read full sectors directly to buffer if possible */
	while (sector_count > 0 && (len - copy_offset) >= emmc_ctx.sector_size) {
		ret = disk_access_read(EMMC_DEVICE_NAME, buf + copy_offset, start_sector, 1);
		if (ret != 0) {
			LOG_ERR("Failed to read sector %u: %d", start_sector, ret);
			return ret;
		}

		copy_offset += emmc_ctx.sector_size;
		start_sector++;
		sector_count--;
	}

	/* Read last partial sector if needed */
	if (copy_offset < len && sector_count > 0) {
		ret = disk_access_read(EMMC_DEVICE_NAME, sector_buf, start_sector, 1);
		if (ret != 0) {
			LOG_ERR("Failed to read sector %u: %d", start_sector, ret);
			return ret;
		}

		copy_len = len - copy_offset;
		memcpy(buf + copy_offset, sector_buf, copy_len);
		copy_offset += copy_len;
	}

	return copy_offset;
}

/**
 * @brief Get eMMC device size
 */
static ssize_t emmc_get_size(void)
{
	int ret;

	if (!emmc_ctx.initialized) {
		ret = emmc_init();
		if (ret != 0) {
			return ret;
		}
	}

	return (ssize_t)emmc_ctx.sector_count * emmc_ctx.sector_size;
}

/**
 * @brief Get eMMC device name
 */
static const char *emmc_get_name(void)
{
	return EMMC_DEVICE_NAME;
}

/* eMMC device operations */
static const struct disk_device_ops emmc_ops = {
	.init = emmc_init,
	.read = emmc_read,
	.get_size = emmc_get_size,
	.get_name = emmc_get_name,
};

/* eMMC device descriptor */
static const struct disk_device emmc_device = {
	.type = DISK_DEVICE_TYPE_EMMC,
	.ops = &emmc_ops,
};

/**
 * @brief Get eMMC device descriptor
 */
const struct disk_device *disk_device_get_emmc(void)
{
	return &emmc_device;
}
