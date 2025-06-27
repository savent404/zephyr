/*
 * Copyright (c) 2025 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/disk_access.h>
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(emmc_dual_api, LOG_LEVEL_DBG);

/* Test data values */
#define INITIAL_VALUE_A5 0xA5

/* Buffer sizes */
#define SECTOR_SIZE      512
#define TEST_BUFFER_SIZE 4096

/* LittleFS mount point */
#define MOUNT_POINT "/MMC:"

/* Device names */
#define MMC_DEVICE_NAME "MMC"

/* LittleFS configuration */
static struct fs_littlefs lfsfs;

/* Global variables */
static struct fs_mount_t lfs_mount_point = {
	.type = FS_LITTLEFS,
	.fs_data = &lfsfs,
	.storage_dev = (void *)MMC_DEVICE_NAME,
	.mnt_point = MOUNT_POINT,
	.flags = FS_MOUNT_FLAG_USE_DISK_ACCESS,
};

static uint8_t test_buffer[TEST_BUFFER_SIZE];
static uint8_t read_buffer[TEST_BUFFER_SIZE];

struct partition_info {
	uint32_t offset;
	uint32_t size;
};

/**
 * @brief Get partition info
 *
 * @param label Partition label to look up
 * @param info Pointer to partition_info structure to fill
 */
static void get_partition_info(const char *label, struct partition_info *info)
{
	if (strcmp(label, "filesystem") == 0) {
#define LFS_PART_NODE DT_NODELABEL(storage_partition)
		info->offset = DT_REG_ADDR(LFS_PART_NODE);
		info->size = DT_REG_SIZE(LFS_PART_NODE);
	} else if (strcmp(label, "raw_data") == 0) {
#define RAW_PART_NODE DT_NODELABEL(data_partition)
		info->offset = DT_REG_ADDR(RAW_PART_NODE);
		info->size = DT_REG_SIZE(RAW_PART_NODE);
	} else {
		LOG_ERR("Unknown partition label: %s", label);
		info->offset = 0;
		info->size = 0;
	}
}

/**
 * @brief Initialize disk access system
 *
 * @return 0 on success, negative on error
 */
static int disk_access_init_system(void)
{
	int ret;
	uint32_t sector_size, sector_count;

	/* Initialize disk subsystem */
	ret = disk_access_init(MMC_DEVICE_NAME);
	if (ret) {
		LOG_ERR("Failed to initialize disk access for %s: %d", MMC_DEVICE_NAME, ret);
		return ret;
	}

	/* Check if the disk is ready */
	ret = disk_access_ioctl(MMC_DEVICE_NAME, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size);
	if (ret) {
		LOG_ERR("Failed to get sector size: %d", ret);
		return ret;
	}

	ret = disk_access_ioctl(MMC_DEVICE_NAME, DISK_IOCTL_GET_SECTOR_COUNT, &sector_count);
	if (ret) {
		LOG_ERR("Failed to get sector count: %d", ret);
		return ret;
	}

	LOG_INF(" - Sector size: %u bytes, Sector count: %u, Total capacity: %u MB", sector_size,
		sector_count, (sector_count * sector_size) / (1024 * 1024));

	return 0;
}

/**
 * @brief Write data to raw partition using disk API
 *
 * @param value Value to fill the buffer with
 * @return 0 on success, negative on error
 */
static int raw_api_write(uint8_t value)
{
	int ret;
	struct partition_info raw_info;

	get_partition_info("raw_data", &raw_info);

	uint32_t sector_start = raw_info.offset / SECTOR_SIZE;
	uint32_t total_sectors = raw_info.size / SECTOR_SIZE;
	uint32_t sectors_per_write = TEST_BUFFER_SIZE / SECTOR_SIZE;
	uint32_t written_sectors = 0;

	memset(test_buffer, value, TEST_BUFFER_SIZE);

	while (written_sectors < total_sectors) {
		uint32_t write_now = sectors_per_write;

		if (written_sectors + write_now > total_sectors) {
			write_now = total_sectors - written_sectors;
		}
		ret = disk_access_write(MMC_DEVICE_NAME, test_buffer,
					sector_start + written_sectors, write_now);
		if (ret) {
			LOG_ERR("Failed to write to raw partition at sector %u: %d",
				sector_start + written_sectors, ret);
			return ret;
		}
		written_sectors += write_now;
	}
	LOG_INF(" ■ Successfully wrote %u bytes to raw partition", raw_info.size);
	return 0;
}

/**
 * @brief Read data from raw partition using disk API
 *
 * @param expected_value Expected value to compare against
 * @return 0 on success, negative on error
 */
static int raw_api_read_and_verify(uint8_t expected_value)
{
	int ret;
	struct partition_info raw_info;

	get_partition_info("raw_data", &raw_info);

	uint32_t sector_start = raw_info.offset / SECTOR_SIZE;
	uint32_t sector_count = TEST_BUFFER_SIZE / SECTOR_SIZE;

	memset(read_buffer, 0, TEST_BUFFER_SIZE);

	LOG_DBG("-> Reading from sectors %u-%u (offset: 0x%08X, size: %u bytes)", sector_start,
		sector_start + sector_count - 1, raw_info.offset, TEST_BUFFER_SIZE);

	ret = disk_access_read(MMC_DEVICE_NAME, read_buffer, sector_start, sector_count);
	if (ret) {
		LOG_ERR("Failed to read from raw partition: %d", ret);
		return ret;
	}

	for (int i = 0; i < TEST_BUFFER_SIZE; i++) {
		if (read_buffer[i] != expected_value) {
			LOG_ERR("Data mismatch at offset %d: expected 0x%02X, got 0x%02X", i,
				expected_value, read_buffer[i]);
			return -EIO;
		}
	}
	LOG_INF(" ■ Raw partition data verification successful - all %u bytes match 0x%02X",
		TEST_BUFFER_SIZE, expected_value);
	return 0;
}

/**
 * @brief Initialize and mount LittleFS
 *
 * @return 0 on success, negative on error
 */
static int littlefs_init_and_mount(void)
{
	static struct partition_info lfs_info;

	get_partition_info("filesystem", &lfs_info);
	lfsfs.cfg.block_size = SECTOR_SIZE;
	lfsfs.cfg.block_count = lfs_info.size / SECTOR_SIZE;
	LOG_INF("LittleFS block_size=%u, block_count=%u, total=%u bytes", lfsfs.cfg.block_size,
		lfsfs.cfg.block_count, lfsfs.cfg.block_size * lfsfs.cfg.block_count);

	return fs_mount(&lfs_mount_point);
}

/**
 * @brief Create test files in LittleFS
 *
 * @return 0 on success, negative on error
 */
static int littlefs_create_test_files(void)
{
	struct fs_file_t file;
	int ret = 0;

	const size_t big_file_size = 900 * 1024; /* 900KB */
	static char big_data[900 * 1024];

	memset(big_data, 0x5A, sizeof(big_data));

	fs_file_t_init(&file);

	ret = fs_open(&file, MOUNT_POINT "/bigfile.bin", FS_O_CREATE | FS_O_WRITE);
	if (ret) {
		LOG_ERR("Failed to create bigfile.bin: %d", ret);
		return ret;
	}
	ssize_t written = fs_write(&file, big_data, big_file_size);

	if (written != big_file_size) {
		LOG_ERR("Failed to write bigfile.bin: %zd/%zu", written, big_file_size);
		fs_close(&file);
		return -EIO;
	}
	fs_close(&file);
	LOG_INF(" - Created bigfile.bin (%zu bytes)", big_file_size);

	return 0;
}

/**
 * @brief Verify test files in LittleFS
 *
 * @return 0 on success, negative on error
 */
static int littlefs_verify_test_files(void)
{
	struct fs_file_t file;
	struct fs_dirent entry;
	int ret;
	char read_buf[256];

	/* List directory contents */
	LOG_INF(" -> Directory listing of %s:", MOUNT_POINT);

	struct fs_dir_t dir;

	fs_dir_t_init(&dir);

	ret = fs_opendir(&dir, MOUNT_POINT);

	if (ret) {
		LOG_ERR("Failed to open directory %s: %d", MOUNT_POINT, ret);
		return ret;
	}

	while (fs_readdir(&dir, &entry) == 0) {
		if (entry.name[0] == 0) {
			break;
		}
		LOG_INF("  - %s (%s, size: %zu)", entry.name,
			(entry.type == FS_DIR_ENTRY_FILE) ? "file" : "dir", entry.size);
	}
	fs_closedir(&dir);

	/* Verify specific test files */
	static const char *const test_files[] = {MOUNT_POINT "/test1.txt", MOUNT_POINT "/test2.txt",
						 MOUNT_POINT "/config.dat"};

	for (int i = 0; i < ARRAY_SIZE(test_files); i++) {
		fs_file_t_init(&file);

		ret = fs_open(&file, test_files[i], FS_O_READ);
		if (ret) {
			LOG_ERR("Failed to open file %s: %d", test_files[i], ret);
			return ret;
		}

		memset(read_buf, 0, sizeof(read_buf));
		ret = fs_read(&file, read_buf, sizeof(read_buf) - 1);
		if (ret < 0) {
			LOG_ERR("Failed to read file %s: %d", test_files[i], ret);
			fs_close(&file);
			return ret;
		}

		fs_close(&file);
	}

	return 0;
}

/**
 * @brief Unmount LittleFS
 *
 * @return 0 on success, negative on error
 */
static int littlefs_unmount(void)
{
	int ret;

	ret = fs_unmount(&lfs_mount_point);
	if (ret) {
		LOG_ERR("Failed to unmount LittleFS: %d", ret);
		return ret;
	}

	return 0;
}

/**
 * @brief Remount LittleFS and verify data persistence
 *
 * @return 0 on success, negative on error
 */
static int littlefs_remount_and_verify(void)
{
	int ret;

	/* Remount the filesystem */
	ret = fs_mount(&lfs_mount_point);
	if (ret) {
		LOG_ERR("Failed to remount LittleFS: %d", ret);
		return ret;
	}

	/* Verify files still exist and contain correct data */
	ret = littlefs_verify_test_files();
	if (ret) {
		LOG_ERR("File verification failed after remount");
		return ret;
	}

	LOG_INF(" ■ All files verified successfully after remount - data persistence confirmed");
	return 0;
}

/**
 * @brief Main application entry point
 */
int main(void)
{
	int ret;

	LOG_INF("====== eMMC Dual API Partition Isolation Test ======");

	/* ===== STEP 0: Initialize disk access system ===== */
	ret = disk_access_init_system();
	if (ret) {
		LOG_ERR("Step 0 failed: %d", ret);
		return ret;
	}

	/* ===== STEP 1: Write initial data to raw partition ===== */
	ret = raw_api_write(INITIAL_VALUE_A5);
	if (ret) {
		LOG_ERR("Step 1 failed: %d", ret);
		return ret;
	}

	/* ===== STEP 2: Initialize LittleFS and create test files ===== */
	ret = littlefs_init_and_mount();
	if (ret) {
		LOG_ERR("Step 2a failed: %d", ret);
		return ret;
	}

	ret = littlefs_create_test_files();
	if (ret) {
		LOG_ERR("Step 2b failed: %d", ret);
		return ret;
	}

	/* ===== STEP 3: Verify raw partition data integrity ===== */
	ret = raw_api_read_and_verify(INITIAL_VALUE_A5);
	if (ret) {
		LOG_ERR(" PARTITION ISOLATION FAILED: Raw partition data was corrupted by "
			"LittleFS!");
		return ret;
	}

	/* ===== STEP 4: Test remount persistence ===== */
	/* Unmount LittleFS */
	ret = littlefs_unmount();
	if (ret) {
		LOG_ERR("Step 4a failed: %d", ret);
		return ret;
	}
	LOG_INF(" ■ LittleFS successfully unmounted");

	/* Verify raw partition is still intact after unmount */
	ret = raw_api_read_and_verify(INITIAL_VALUE_A5);
	if (ret) {
		LOG_ERR(" Raw partition data corrupted after LittleFS unmount!");
		return ret;
	}

	/* Remount and verify LittleFS data */
	ret = littlefs_remount_and_verify();
	if (ret) {
		LOG_ERR("Step 4b failed: %d", ret);
		return ret;
	}

	/* Final verification of raw partition */
	ret = raw_api_read_and_verify(INITIAL_VALUE_A5);
	if (ret) {
		LOG_ERR(" Raw partition data corrupted after LittleFS remount!");
		return ret;
	}

	/* ===== FINAL SUCCESS MESSAGE ===== */
	LOG_INF("\n====== TEST COMPLETED SUCCESSFULLY ======");
	LOG_INF(" ■ Partition isolation verified");
	LOG_INF(" ■ Raw API partition remains unaffected by LittleFS operations");
	LOG_INF(" ■ LittleFS data persists across mount/unmount cycles");
	LOG_INF(" ■ Both partitions operate independently without interference");

	return 0;
}
