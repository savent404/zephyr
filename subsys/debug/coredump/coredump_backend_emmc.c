/*
 * Copyright (c) 2026 SYSTech Co.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <string.h>
#include <zephyr/toolchain.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/util.h>

#include <zephyr/debug/coredump.h>
#include "coredump_internal.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(coredump_emmc, CONFIG_KERNEL_LOG_LEVEL);

#define LOG_OUT printk

/*
 * Weak watchdog feed hook.
 *
 * The coredump backend does NOT own the watchdog and does not attempt to
 * initialize it. It only provides a high-frequency call-site so user/board
 * code may decide if/when to feed (e.g. throttle by time).
 */
__weak void coredump_watchdog_feed(void)
{
	static bool warned;

	if (!warned) {
		warned = true;
		LOG_WRN("watchdog feed hook not overridden; override %s()", __func__);
	}
}

static inline void emmc_wdt_feed_hook(void)
{
	coredump_watchdog_feed();
}
/**
 * @file
 * @brief Coredump backend to store data in eMMC storage.
 *
 * This provides a backend to store coredump data in eMMC storage
 * using disk access API. The coredump partition should be defined
 * in devicetree with label "coredump-partition".
 *
 * On the partition, a header is stored at the beginning with padding
 * at the end to align with sector size. Then the actual coredump data
 * follows. The padding is to simplify the data read function so that
 * the first read of a data stream is always aligned to sector size.
 *
 * @warning This backend initializes at APPLICATION level, so early boot
 * faults (PRE_KERNEL_1/2, POST_KERNEL) cannot be captured. See Kconfig
 * help text for details and alternatives.
 */

#define EMMC_PARTITION      coredump_partition
#define EMMC_PARTITION_ID   FIXED_PARTITION_ID(EMMC_PARTITION)
#define EMMC_PARTITION_NODE DT_NODELABEL(EMMC_PARTITION)

#if !DT_NODE_EXISTS(EMMC_PARTITION_NODE)
#error "Need a fixed partition named 'coredump-partition' for eMMC backend!"
#endif

#if DT_NODE_EXISTS(EMMC_PARTITION_NODE)

/* Sector size for eMMC is typically 512 bytes */
#define SECTOR_SIZE 512

#ifndef CONFIG_DEBUG_COREDUMP_EMMC_CHUNK_SIZE
#define CONFIG_DEBUG_COREDUMP_EMMC_CHUNK_SIZE 512
#endif

#define EMMC_BUF_SIZE MAX(SECTOR_SIZE, ROUND_UP(CONFIG_DEBUG_COREDUMP_EMMC_CHUNK_SIZE, SECTOR_SIZE))

#define HEADER_SECTOR_SIZE ROUND_UP(sizeof(struct emmc_hdr_t), SECTOR_SIZE)

#define HDR_VER 1

#define EMMC_BACKEND_SEM_TIMEOUT (k_is_in_isr() ? K_NO_WAIT : K_FOREVER)

typedef int (*data_read_cb_t)(void *arg, uint8_t *buf, size_t len);

static struct {
	/* eMMC device name */
	const char *device_name;

	/* Partition offset in sectors */
	uint32_t partition_offset;

	/* Partition size in sectors */
	uint32_t partition_size;

	/* Current write position in bytes (relative to partition start) */
	size_t write_pos;

	/* Checksum of data so far */
	uint16_t checksum;

	/* Error encountered */
	int error;

	/* Device initialized flag */
	bool initialized;
} backend_ctx = {
#ifdef CONFIG_DEBUG_COREDUMP_EMMC_DEVICE_NAME
	.device_name = CONFIG_DEBUG_COREDUMP_EMMC_DEVICE_NAME,
#else
	.device_name = "MMC",
#endif
};

/* Buffer used for writing */
static uint8_t write_buf[EMMC_BUF_SIZE];
static size_t write_buf_pos;

/* Buffer used in data_read() */
static uint8_t read_buf[EMMC_BUF_SIZE];

/* Semaphore for exclusive eMMC access */
K_SEM_DEFINE(emmc_sem, 1, 1);

struct emmc_hdr_t {
	/* 'C', 'D' */
	char id[2];

	/* Header version */
	uint16_t hdr_version;

	/* Coredump size, excluding this header */
	size_t size;

	/* Flags */
	uint16_t flags;

	/* Checksum */
	uint16_t checksum;

	/* Error */
	int error;
} __packed;

/**
 * @brief Initialize eMMC backend.
 *
 * @return 0 on success, negative errno on error.
 */
static int emmc_backend_init(void)
{
	int ret;
	uint32_t sector_size, sector_count;

	if (backend_ctx.initialized) {
		return 0;
	}

	LOG_INF("Initializing eMMC device '%s'...\n", backend_ctx.device_name);

	/* Initialize disk subsystem */
	ret = disk_access_init(backend_ctx.device_name);
	if (ret != 0) {
		LOG_ERR("Failed to initialize disk access for %s: %d", backend_ctx.device_name,
			ret);
		return ret;
	}

	/* Get sector size */
	ret = disk_access_ioctl(backend_ctx.device_name, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size);
	if (ret != 0) {
		LOG_ERR("Failed to get sector size: %d", ret);
		return ret;
	}

	if (sector_size != SECTOR_SIZE) {
		LOG_ERR("Unexpected sector size: %u (expected %u)", sector_size, SECTOR_SIZE);
		return -EINVAL;
	}

	/* Get sector count */
	ret = disk_access_ioctl(backend_ctx.device_name, DISK_IOCTL_GET_SECTOR_COUNT,
				&sector_count);
	if (ret != 0) {
		LOG_ERR("Failed to get sector count: %d", ret);
		return ret;
	}

	/* Get partition info from devicetree */
	backend_ctx.partition_offset = DT_REG_ADDR(EMMC_PARTITION_NODE) / SECTOR_SIZE;
	backend_ctx.partition_size = DT_REG_SIZE(EMMC_PARTITION_NODE) / SECTOR_SIZE;

	LOG_INF("eMMC coredump partition: offset=%u sectors, size=%u sectors",
		backend_ctx.partition_offset, backend_ctx.partition_size);

	backend_ctx.initialized = true;

	return 0;
}

/**
 * @brief Write sectors to eMMC.
 *
 * @param sector_offset Sector offset relative to partition start
 * @param buf Buffer to write
 * @param sector_count Number of sectors to write
 * @return 0 on success, negative errno on error.
 */
static int emmc_write_sectors(uint32_t sector_offset, const uint8_t *buf, uint32_t sector_count)
{
	uint32_t absolute_sector = backend_ctx.partition_offset + sector_offset;

	if (sector_offset + sector_count > backend_ctx.partition_size) {
		LOG_ERR("Write beyond partition boundary");
		return -EINVAL;
	}

	return disk_access_write(backend_ctx.device_name, buf, absolute_sector, sector_count);
}

/**
 * @brief Read sectors from eMMC.
 *
 * @param sector_offset Sector offset relative to partition start
 * @param buf Buffer to read into
 * @param sector_count Number of sectors to read
 * @return 0 on success, negative errno on error.
 */
static int emmc_read_sectors(uint32_t sector_offset, uint8_t *buf, uint32_t sector_count)
{
	uint32_t absolute_sector = backend_ctx.partition_offset + sector_offset;

	if (sector_offset + sector_count > backend_ctx.partition_size) {
		LOG_ERR("Read beyond partition boundary");
		return -EINVAL;
	}

	return disk_access_read(backend_ctx.device_name, buf, absolute_sector, sector_count);
}

/**
 * @brief Erase the eMMC partition by writing zeros.
 *
 * @return 0 on success, negative errno on error.
 */
static int erase_emmc_partition(void)
{
	int ret;
	uint32_t sector;
	uint8_t zero_buf[SECTOR_SIZE];

	ret = emmc_backend_init();
	if (ret != 0) {
		return ret;
	}

	(void)k_sem_take(&emmc_sem, EMMC_BACKEND_SEM_TIMEOUT);

	memset(zero_buf, 0, sizeof(zero_buf));

	/* Erase first few sectors (header area) */
	for (sector = 0; sector < 8 && sector < backend_ctx.partition_size; sector++) {
		ret = emmc_write_sectors(sector, zero_buf, 1);
		if (ret != 0) {
			LOG_ERR("Failed to erase sector %u: %d", sector, ret);
			k_sem_give(&emmc_sem);
			return ret;
		}
	}

	k_sem_give(&emmc_sem);

	LOG_INF("eMMC coredump partition erased");
	return 0;
}

/**
 * @brief Invalidate stored coredump by clearing header.
 *
 * @return 0 on success, negative errno on error.
 */
static int erase_coredump_header(void)
{
	int ret;
	uint8_t zero_buf[SECTOR_SIZE];

	ret = emmc_backend_init();
	if (ret != 0) {
		return ret;
	}

	(void)k_sem_take(&emmc_sem, EMMC_BACKEND_SEM_TIMEOUT);

	memset(zero_buf, 0, sizeof(zero_buf));
	ret = emmc_write_sectors(0, zero_buf, 1);

	k_sem_give(&emmc_sem);

	if (ret == 0) {
		LOG_INF("Coredump header invalidated");
	} else {
		LOG_ERR("Failed to invalidate header: %d", ret);
	}

	return ret;
}

/**
 * @brief Start of coredump session.
 *
 * This initializes the eMMC backend and prepares for writing.
 */
static void coredump_emmc_backend_start(void)
{
	int ret;

	LOG_OUT("\n=== Coredump eMMC Backend Start ===\n");

	ret = emmc_backend_init();
	if (ret != 0) {
		LOG_ERR("ERROR: eMMC init failed: %d\n", ret);
		backend_ctx.error = ret;
		return;
	}

	(void)k_sem_take(&emmc_sem, EMMC_BACKEND_SEM_TIMEOUT);

	/* High-frequency feed call-site; user decides whether to actually feed. */
	emmc_wdt_feed_hook();

	backend_ctx.write_pos = HEADER_SECTOR_SIZE;
	backend_ctx.checksum = 0;
	backend_ctx.error = 0;
	write_buf_pos = 0;

	LOG_INF("Coredump eMMC backend started (partition: %u sectors, %u KB)",
		backend_ctx.partition_size, backend_ctx.partition_size * SECTOR_SIZE / 1024);
	LOG_INF("This may take a while for large memory dumps...");
}

/**
 * @brief Flush write buffer to eMMC.
 *
 * @param final Set to true for final flush
 * @return 0 on success, negative errno on error.
 */
static int flush_write_buffer(bool final)
{
	int ret = 0;
	size_t bytes_to_write;
	uint32_t sectors;
	uint32_t sector_offset;
	size_t max_allowed_bytes;

	if (write_buf_pos == 0) {
		return 0;
	}

	if (final) {
		/* Pad to sector boundary */
		bytes_to_write = ROUND_UP(write_buf_pos, SECTOR_SIZE);
		if (bytes_to_write > write_buf_pos) {
			memset(&write_buf[write_buf_pos], 0, bytes_to_write - write_buf_pos);
		}
	} else {
		/* Only write complete sectors */
		bytes_to_write = (write_buf_pos / SECTOR_SIZE) * SECTOR_SIZE;
		if (bytes_to_write == 0) {
			return 0;
		}
	}

	/* Check if write would exceed partition size */
	sector_offset = backend_ctx.write_pos / SECTOR_SIZE;
	sectors = bytes_to_write / SECTOR_SIZE;
	max_allowed_bytes = backend_ctx.partition_size * SECTOR_SIZE;

	if (sector_offset + sectors > backend_ctx.partition_size) {
		LOG_OUT("ERROR: Write would exceed partition!\n");
		LOG_OUT("  Current pos: %zu bytes (%u sectors)\n", backend_ctx.write_pos,
			sector_offset);
		LOG_OUT("  Write size: %zu bytes (%u sectors)\n", bytes_to_write, sectors);
		LOG_OUT("  Partition size: %zu bytes (%u sectors)\n", max_allowed_bytes,
			backend_ctx.partition_size);
		LOG_OUT("  Overflow: %zu bytes\n",
			(sector_offset + sectors - backend_ctx.partition_size) * SECTOR_SIZE);
		LOG_ERR("Coredump exceeds partition size!");
		return -ENOSPC;
	}

	/* High-frequency feed call-site; user decides whether to actually feed. */
	emmc_wdt_feed_hook();
	ret = emmc_write_sectors(sector_offset, write_buf, sectors);
	emmc_wdt_feed_hook();
	if (ret != 0) {
		LOG_ERR("Failed to write to eMMC: %d", ret);
		return ret;
	}

	backend_ctx.write_pos += bytes_to_write;

	/* Log progress every 1MB for large dumps */
	if ((backend_ctx.write_pos % (1024 * 1024)) < SECTOR_SIZE) {
		LOG_INF("Coredump progress: %zu KB written", backend_ctx.write_pos / 1024);
	}

	/* Move remaining data to beginning of buffer */
	if (!final && bytes_to_write < write_buf_pos) {
		size_t remaining = write_buf_pos - bytes_to_write;

		memmove(write_buf, &write_buf[bytes_to_write], remaining);
		write_buf_pos = remaining;
	} else {
		write_buf_pos = 0;
	}

	return 0;
}

/**
 * @brief End of coredump session.
 *
 * This flushes the write buffer and writes the header.
 */
static void coredump_emmc_backend_end(void)
{
	int ret;
	size_t actual_data_size;
	struct emmc_hdr_t hdr = {
		.id = {'C', 'D'},
		.hdr_version = HDR_VER,
	};
	uint8_t hdr_buf[SECTOR_SIZE];

	if (backend_ctx.error != 0) {
		goto cleanup;
	}

	/* Save actual data size before padding */
	actual_data_size = (backend_ctx.write_pos - HEADER_SECTOR_SIZE) + write_buf_pos;

	/* Flush remaining data */
	emmc_wdt_feed_hook();
	ret = flush_write_buffer(true);
	if (ret != 0) {
		backend_ctx.error = ret;
		goto cleanup;
	}

	/* Prepare header with actual data size (excluding padding) */
	hdr.size = actual_data_size;
	hdr.checksum = backend_ctx.checksum;
	hdr.error = backend_ctx.error;
	hdr.flags = 0;

	LOG_INF("Coredump: actual_data_size=%zu, write_pos=%lu, padded_size=%lu", actual_data_size,
		(unsigned long)(backend_ctx.write_pos - HEADER_SECTOR_SIZE),
		(unsigned long)(backend_ctx.write_pos - HEADER_SECTOR_SIZE));

	/* Write header to first sector */
	memset(hdr_buf, 0, sizeof(hdr_buf));
	memcpy(hdr_buf, &hdr, sizeof(hdr));

	ret = emmc_write_sectors(0, hdr_buf, 1);
	if (ret != 0) {
		LOG_ERR("Failed to write coredump header: %d", ret);
		backend_ctx.error = ret;
	}

cleanup:
	if (backend_ctx.error != 0) {
		LOG_OUT("\nERROR: Coredump failed with error: %d\n", backend_ctx.error);
		LOG_ERR("Error in coredump eMMC backend: %d", backend_ctx.error);
	} else {
		LOG_OUT("\n=== Coredump Complete ===\n");
		LOG_OUT("Saved %zu bytes to eMMC\n", hdr.size);
		LOG_OUT("Use 'cdump info' to verify\n");
		LOG_INF("Coredump saved to eMMC (%zu bytes)", hdr.size);
	}

	k_sem_give(&emmc_sem);
}

/**
 * @brief Write a buffer to eMMC.
 *
 * This buffers data and writes to eMMC in sector-aligned chunks.
 *
 * @param buf Buffer of data to write
 * @param buflen Number of bytes to write
 */
static void coredump_emmc_backend_buffer_output(uint8_t *buf, size_t buflen)
{
	int i;
	size_t remaining = buflen;
	uint8_t *ptr = buf;
	uint8_t tmp_buf[EMMC_BUF_SIZE];
	size_t copy_sz;

	if (backend_ctx.error != 0) {
		return;
	}

	/*
	 * Make a copy of the data to ensure consistency, as memory
	 * content may change during execution.
	 */
	while (remaining > 0) {
		emmc_wdt_feed_hook();
		copy_sz = MIN(remaining, EMMC_BUF_SIZE);

		/* Copy to temporary buffer */
		memmove(tmp_buf, ptr, copy_sz);

		/* Update checksum */
		for (i = 0; i < copy_sz; i++) {
			backend_ctx.checksum += tmp_buf[i];
		}

		/* Add to write buffer */
		size_t space_in_buffer = EMMC_BUF_SIZE - write_buf_pos;
		size_t to_copy = MIN(copy_sz, space_in_buffer);

		memcpy(&write_buf[write_buf_pos], tmp_buf, to_copy);
		write_buf_pos += to_copy;

		/* Flush if buffer is full */
		if (write_buf_pos >= EMMC_BUF_SIZE) {
			emmc_wdt_feed_hook();
			backend_ctx.error = flush_write_buffer(false);
			if (backend_ctx.error != 0) {
				return;
			}
		}

		/* Handle remaining data if it didn't fit */
		if (to_copy < copy_sz) {
			size_t remaining_copy = copy_sz - to_copy;

			memcpy(&write_buf[write_buf_pos], &tmp_buf[to_copy], remaining_copy);
			write_buf_pos += remaining_copy;
		}

		ptr += copy_sz;
		remaining -= copy_sz;
	}
}

/**
 * @brief Read data from eMMC partition.
 *
 * @param off Offset in bytes from partition start
 * @param dst Destination buffer (can be NULL)
 * @param len Number of bytes to read
 * @param cb Callback for processing data (can be NULL)
 * @param cb_arg Argument for callback
 * @return 0 on success, negative errno on error.
 */
static int data_read(off_t off, uint8_t *dst, size_t len, data_read_cb_t cb, void *cb_arg)
{
	int ret = 0;
	off_t offset = off;
	size_t remaining = len;
	size_t copy_sz;
	uint8_t *ptr = dst;
	uint32_t sector_offset;
	uint32_t sectors_to_read;

	ret = emmc_backend_init();
	if (ret != 0) {
		return ret;
	}

	while (remaining > 0) {
		copy_sz = MIN(remaining, EMMC_BUF_SIZE);
		sector_offset = offset / SECTOR_SIZE;
		sectors_to_read = ROUND_UP(copy_sz, SECTOR_SIZE) / SECTOR_SIZE;

		ret = emmc_read_sectors(sector_offset, read_buf, sectors_to_read);
		if (ret != 0) {
			LOG_ERR("Failed to read from eMMC: %d", ret);
			break;
		}

		size_t offset_in_sector = offset % SECTOR_SIZE;
		size_t available = MIN(copy_sz, sectors_to_read * SECTOR_SIZE - offset_in_sector);

		if (dst != NULL) {
			memcpy(ptr, &read_buf[offset_in_sector], available);
			ptr += available;
		}

		if (cb != NULL) {
			ret = (*cb)(cb_arg, &read_buf[offset_in_sector], available);
			if (ret != 0) {
				break;
			}
		}

		offset += available;
		remaining -= available;
	}

	if (cb != NULL && ret == 0) {
		ret = (*cb)(cb_arg, NULL, 0);
	}

	return ret;
}

/**
 * @brief Callback to calculate checksum.
 *
 * @param arg Callback argument (not used)
 * @param buf Data buffer
 * @param len Number of bytes to process
 * @return 0
 */
static int cb_calc_buf_checksum(void *arg, uint8_t *buf, size_t len)
{
	int i;

	ARG_UNUSED(arg);

	if (buf == NULL) {
		return 0;
	}

	for (i = 0; i < len; i++) {
		backend_ctx.checksum += buf[i];
	}

	return 0;
}

/**
 * @brief Process stored coredump.
 *
 * @param cb Callback for processing data
 * @param cb_arg Argument for callback
 * @return 1 if valid dump found, 0 if not, negative on error.
 */
static int process_stored_dump(data_read_cb_t cb, void *cb_arg)
{
	int ret;
	struct emmc_hdr_t hdr;

	ret = emmc_backend_init();
	if (ret != 0) {
		return ret;
	}

	(void)k_sem_take(&emmc_sem, EMMC_BACKEND_SEM_TIMEOUT);

	/* Read header */
	ret = data_read(0, (uint8_t *)&hdr, sizeof(hdr), NULL, NULL);
	if (ret != 0) {
		k_sem_give(&emmc_sem);
		return ret;
	}

	LOG_DBG("%s: id=%c%c ver=%u size=%zu flags=0x%04x csum=0x%04x err=%d", __func__, hdr.id[0],
		hdr.id[1], hdr.hdr_version, hdr.size, hdr.flags, hdr.checksum, hdr.error);

	/* Verify header signature */
	if (hdr.id[0] != 'C' || hdr.id[1] != 'D') {
		LOG_WRN("Invalid header signature: %c%c (expected CD)", hdr.id[0], hdr.id[1]);
		k_sem_give(&emmc_sem);
		return 0;
	}

	/* Error encountered while dumping */
	if (hdr.error != 0) {
		LOG_WRN("Header indicates error during dump: %d", hdr.error);
		k_sem_give(&emmc_sem);
		return 0;
	}

	backend_ctx.checksum = 0;

	/* Read and process coredump data */
	ret = data_read(HEADER_SECTOR_SIZE, NULL, hdr.size, cb, cb_arg);

	k_sem_give(&emmc_sem);

	if (ret == 0) {
		ret = (backend_ctx.checksum == hdr.checksum) ? 1 : 0;
	}

	return ret;
}

/**
 * @brief Get stored coredump.
 *
 * @param off Offset to read from
 * @param dst Destination buffer (NULL to get size)
 * @param len Number of bytes to read
 * @return Size or bytes read on success, 0 if no dump, negative on error.
 */
static int get_stored_dump(off_t off, uint8_t *dst, size_t len)
{
	int ret;
	struct emmc_hdr_t hdr;

	ret = emmc_backend_init();
	if (ret != 0) {
		return ret;
	}

	(void)k_sem_take(&emmc_sem, EMMC_BACKEND_SEM_TIMEOUT);

	/* Read header */
	ret = data_read(0, (uint8_t *)&hdr, sizeof(hdr), NULL, NULL);
	if (ret != 0) {
		k_sem_give(&emmc_sem);
		return ret;
	}

	LOG_DBG("%s: id=%c%c ver=%u size=%zu flags=0x%04x csum=0x%04x err=%d", __func__, hdr.id[0],
		hdr.id[1], hdr.hdr_version, hdr.size, hdr.flags, hdr.checksum, hdr.error);

	/* Verify header signature */
	if (hdr.id[0] != 'C' || hdr.id[1] != 'D') {
		LOG_WRN("Invalid header signature: %c%c (expected CD)", hdr.id[0], hdr.id[1]);
		k_sem_give(&emmc_sem);
		return 0;
	}

	/* Error encountered while dumping */
	if (hdr.error != 0) {
		LOG_WRN("Header indicates error during dump: %d", hdr.error);
		k_sem_give(&emmc_sem);
		return 0;
	}

	/* Return size if no destination buffer */
	if (dst == NULL) {
		k_sem_give(&emmc_sem);
		return (int)hdr.size;
	}

	/* Check offset bounds */
	if (off >= hdr.size) {
		k_sem_give(&emmc_sem);
		return 0;
	}

	/* Read data */
	ret = data_read(HEADER_SECTOR_SIZE + off, dst, len, NULL, NULL);

	k_sem_give(&emmc_sem);

	if (ret == 0) {
		ret = (int)len;
	}

	return ret;
}

/**
 * @brief Perform query on eMMC backend.
 *
 * @param query_id Query ID
 * @param arg Query argument
 * @return Depends on query
 */
static int coredump_emmc_backend_query(enum coredump_query_id query_id, void *arg)
{
	int ret;

	switch (query_id) {
	case COREDUMP_QUERY_GET_ERROR:
		ret = backend_ctx.error;
		break;
	case COREDUMP_QUERY_HAS_STORED_DUMP:
		ret = process_stored_dump(cb_calc_buf_checksum, NULL);
		break;
	case COREDUMP_QUERY_GET_STORED_DUMP_SIZE:
		ret = get_stored_dump(0, NULL, 0);
		break;
	default:
		ret = -ENOTSUP;
		break;
	}

	return ret;
}

/**
 * @brief Perform command on eMMC backend.
 *
 * @param cmd_id Command ID
 * @param arg Command argument
 * @return Depends on command
 */
static int coredump_emmc_backend_cmd(enum coredump_cmd_id cmd_id, void *arg)
{
	int ret;

	switch (cmd_id) {
	case COREDUMP_CMD_CLEAR_ERROR:
		ret = 0;
		backend_ctx.error = 0;
		break;
	case COREDUMP_CMD_VERIFY_STORED_DUMP:
		ret = process_stored_dump(cb_calc_buf_checksum, NULL);
		break;
	case COREDUMP_CMD_ERASE_STORED_DUMP:
		ret = erase_emmc_partition();
		break;
	case COREDUMP_CMD_COPY_STORED_DUMP:
		if (arg != NULL) {
			struct coredump_cmd_copy_arg *copy_arg =
				(struct coredump_cmd_copy_arg *)arg;

			ret = get_stored_dump(copy_arg->offset, copy_arg->buffer, copy_arg->length);
		} else {
			ret = -EINVAL;
		}
		break;
	case COREDUMP_CMD_INVALIDATE_STORED_DUMP:
		ret = erase_coredump_header();
		break;
	default:
		ret = -ENOTSUP;
		break;
	}

	return ret;
}

/**
 * @brief Debug: Read raw sector from partition
 * This is for debugging purposes only
 */
int coredump_emmc_read_raw_sector(uint32_t sector, uint8_t *buf, uint32_t count)
{
	int ret;

	ret = emmc_backend_init();
	if (ret != 0) {
		return ret;
	}

	(void)k_sem_take(&emmc_sem, EMMC_BACKEND_SEM_TIMEOUT);
	ret = emmc_read_sectors(sector, buf, count);
	k_sem_give(&emmc_sem);

	return ret;
}

struct coredump_backend_api coredump_backend_emmc = {
	.start = coredump_emmc_backend_start,
	.end = coredump_emmc_backend_end,
	.buffer_output = coredump_emmc_backend_buffer_output,
	.query = coredump_emmc_backend_query,
	.cmd = coredump_emmc_backend_cmd,
};

/**
 * @brief Initialize eMMC backend at APPLICATION level.
 *
 * Called just before main() to ensure disk subsystem is fully ready.
 * disk_access_init() may block waiting for hardware, which would hang
 * if called in fault/ISR context.
 *
 * @note Early boot faults (before APPLICATION init) cannot be captured.
 *
 * @return 0 on success, negative errno on error.
 */
static int coredump_emmc_backend_sys_init(void)
{
	int ret;

	LOG_INF("Initializing coredump eMMC backend (APPLICATION level)...");
	LOG_INF("Note: Early boot faults (before APPLICATION init) cannot be captured");

	ret = emmc_backend_init();
	if (ret != 0) {
		LOG_ERR("Failed to initialize eMMC backend: %d", ret);
		LOG_WRN("Coredump will NOT work - eMMC backend initialization failed!");
		LOG_WRN("Faults will not be captured to eMMC storage.");
		return ret;
	}

	LOG_INF("Coredump eMMC backend ready - fault capture enabled");
	LOG_DBG("eMMC backend initialized at APPLICATION level");
	LOG_DBG("Coredump coverage: faults from APPLICATION init onwards");
	return 0;
}

/* Initialize at APPLICATION level to ensure disk subsystem is ready.
 * Trade-off: Reliable eMMC access vs. no early boot fault coverage.
 */
SYS_INIT(coredump_emmc_backend_sys_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

#endif /* DT_NODE_EXISTS(EMMC_PARTITION_NODE) */
