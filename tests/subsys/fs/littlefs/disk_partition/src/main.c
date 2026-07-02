/*
 * Copyright (c) 2026 SYSTech Co.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/disk.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/ztest.h>

#define RAM_DISK_NODE        DT_NODELABEL(ramdisk0)
#define LFS0_NODE            DT_NODELABEL(lfs0_partition)
#define LFS1_NODE            DT_NODELABEL(lfs1_partition)
#define PARENT_DISK_NAME     DT_PROP(RAM_DISK_NODE, disk_name)
#define SECTOR_SIZE          512U
#define PARTITION_SIZE_128MB (128U * 1024U * 1024U)
#define LFS0_FILESYSTEM_SIZE PARTITION_SIZE_128MB
#define LFS1_FILESYSTEM_SIZE PARTITION_SIZE_128MB

BUILD_ASSERT(DT_REG_SIZE(LFS0_NODE) == PARTITION_SIZE_128MB);
BUILD_ASSERT(DT_REG_SIZE(LFS1_NODE) == PARTITION_SIZE_128MB);
BUILD_ASSERT(LFS0_FILESYSTEM_SIZE == DT_REG_SIZE(LFS0_NODE));
BUILD_ASSERT(LFS1_FILESYSTEM_SIZE == DT_REG_SIZE(LFS1_NODE));
BUILD_ASSERT(DT_REG_ADDR(LFS1_NODE) == DT_REG_ADDR(LFS0_NODE) + DT_REG_SIZE(LFS0_NODE));

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
	int rc;

	if (part->sector_size != 0U) {
		return 0;
	}

	rc = disk_access_init(part->parent_name);
	if (rc < 0) {
		return rc;
	}

	rc = disk_access_ioctl(part->parent_name, DISK_IOCTL_GET_SECTOR_SIZE, &part->sector_size);
	if (rc < 0) {
		return rc;
	}

	rc = disk_access_ioctl(part->parent_name, DISK_IOCTL_GET_SECTOR_COUNT,
			       &parent_sector_count);
	if (rc < 0) {
		return rc;
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
	int rc;

	rc = partition_setup(part);
	if (rc < 0) {
		return rc;
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
	int rc;

	rc = partition_setup(part);
	if (rc < 0) {
		return rc;
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
	int rc;

	switch (cmd) {
	case DISK_IOCTL_CTRL_INIT:
		return partition_setup(part);
	case DISK_IOCTL_GET_SECTOR_COUNT:
		rc = partition_setup(part);
		if (rc < 0) {
			return rc;
		}
		*(uint32_t *)buff = part->sector_count;
		return 0;
	case DISK_IOCTL_GET_SECTOR_SIZE:
		rc = partition_setup(part);
		if (rc < 0) {
			return rc;
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
	.parent_name = PARENT_DISK_NAME,
	.offset = DT_REG_ADDR(LFS0_NODE),
	.size = LFS0_FILESYSTEM_SIZE,
};

static struct partition_disk lfs1_disk = {
	.info.name = "LFS1",
	.info.ops = &partition_disk_ops,
	.parent_name = PARENT_DISK_NAME,
	.offset = DT_REG_ADDR(LFS1_NODE),
	.size = LFS1_FILESYSTEM_SIZE,
};

FS_LITTLEFS_DECLARE_CUSTOM_CONFIG(lfs0_data, 4, SECTOR_SIZE, SECTOR_SIZE, SECTOR_SIZE, 128);
FS_LITTLEFS_DECLARE_CUSTOM_CONFIG(lfs1_data, 4, SECTOR_SIZE, SECTOR_SIZE, SECTOR_SIZE, 128);

static struct fs_mount_t lfs0_mount = {
	.type = FS_LITTLEFS,
	.fs_data = &lfs0_data,
	.storage_dev = "LFS0",
	.mnt_point = "/lfs0",
	.flags = FS_MOUNT_FLAG_USE_DISK_ACCESS,
};

static struct fs_mount_t lfs1_mount = {
	.type = FS_LITTLEFS,
	.fs_data = &lfs1_data,
	.storage_dev = "LFS1",
	.mnt_point = "/lfs1",
	.flags = FS_MOUNT_FLAG_USE_DISK_ACCESS,
};

static void verify_disk_size(const char *disk_name, uint32_t expected_size)
{
	uint32_t sector_count;
	uint32_t sector_size;
	uint64_t actual_size;
	int rc;

	rc = disk_access_ioctl(disk_name, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size);
	zassert_equal(rc, 0, "%s sector size query failed: %d", disk_name, rc);

	rc = disk_access_ioctl(disk_name, DISK_IOCTL_GET_SECTOR_COUNT, &sector_count);
	zassert_equal(rc, 0, "%s sector count query failed: %d", disk_name, rc);

	actual_size = (uint64_t)sector_count * sector_size;
	zassert_equal(actual_size, (uint64_t)expected_size,
		      "%s filesystem size %" PRIu64 " != partition size %u", disk_name, actual_size,
		      expected_size);
}

static void verify_fs_read_write(const char *mount_point, const char *payload)
{
	char path[32];
	char read_buf[32] = {0};
	struct fs_file_t file;
	ssize_t rc;

	snprintk(path, sizeof(path), "%s/probe.txt", mount_point);

	fs_file_t_init(&file);
	rc = fs_open(&file, path, FS_O_CREATE | FS_O_RDWR);
	zassert_equal(rc, 0, "open failed at %s: %zd", path, rc);

	rc = fs_write(&file, payload, strlen(payload));
	zassert_equal(rc, strlen(payload), "write failed at %s: %zd", path, rc);

	rc = fs_seek(&file, 0, FS_SEEK_SET);
	zassert_equal(rc, 0, "seek failed at %s: %zd", path, rc);

	rc = fs_read(&file, read_buf, sizeof(read_buf) - 1);
	zassert_equal(rc, strlen(payload), "read failed at %s: %zd", path, rc);
	zassert_mem_equal(read_buf, payload, strlen(payload), "payload mismatch at %s", path);

	rc = fs_close(&file);
	zassert_equal(rc, 0, "close failed at %s: %zd", path, rc);
}

ZTEST(littlefs_disk_partition, test_two_128mb_dt_partitions_can_host_littlefs)
{
	int rc;

	rc = disk_access_register(&lfs0_disk.info);
	zassert_equal(rc, 0, "LFS0 register failed: %d", rc);

	rc = disk_access_register(&lfs1_disk.info);
	zassert_equal(rc, 0, "LFS1 register failed: %d", rc);

	verify_disk_size("LFS0", DT_REG_SIZE(LFS0_NODE));
	verify_disk_size("LFS1", DT_REG_SIZE(LFS1_NODE));

	rc = fs_mount(&lfs0_mount);
	zassert_equal(rc, 0, "lfs0 mount/format failed: %d", rc);

	rc = fs_mount(&lfs1_mount);
	zassert_equal(rc, 0, "lfs1 mount/format failed: %d", rc);

	verify_fs_read_write("/lfs0", "first partition");
	verify_fs_read_write("/lfs1", "second partition");

	rc = fs_unmount(&lfs0_mount);
	zassert_equal(rc, 0, "lfs0 unmount failed: %d", rc);

	rc = fs_unmount(&lfs1_mount);
	zassert_equal(rc, 0, "lfs1 unmount failed: %d", rc);
}

ZTEST_SUITE(littlefs_disk_partition, NULL, NULL, NULL, NULL, NULL);
