/*
 * Copyright (c) 2026 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_DISK_DISK_TOOLS_DISK_DEVICE_H_
#define ZEPHYR_SUBSYS_DISK_DISK_TOOLS_DISK_DEVICE_H_

#include <zephyr/kernel.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Disk device type
 */
enum disk_device_type {
	DISK_DEVICE_TYPE_EMMC,
	DISK_DEVICE_TYPE_FLASH,
	DISK_DEVICE_TYPE_SD,
	DISK_DEVICE_TYPE_RAM,
	DISK_DEVICE_TYPE_UNKNOWN,
};

/**
 * @brief Disk device operations
 */
struct disk_device_ops {
	/**
	 * @brief Initialize the disk device
	 *
	 * @return 0 on success, negative errno on error
	 */
	int (*init)(void);

	/**
	 * @brief Read data from disk device
	 *
	 * @param offset Byte offset to read from
	 * @param buf Buffer to store read data
	 * @param len Number of bytes to read
	 * @return Number of bytes read on success, negative errno on error
	 */
	int (*read)(size_t offset, uint8_t *buf, size_t len);

	/**
	 * @brief Get device size in bytes
	 *
	 * @return Device size in bytes, or negative errno on error
	 */
	ssize_t (*get_size)(void);

	/**
	 * @brief Get device name
	 *
	 * @return Device name string
	 */
	const char *(*get_name)(void);
};

/**
 * @brief Disk device descriptor
 */
struct disk_device {
	enum disk_device_type type;
	const struct disk_device_ops *ops;
};

/**
 * @brief Get disk device by type
 *
 * @param type Disk device type
 * @return Pointer to disk device descriptor, or NULL if not found
 */
const struct disk_device *disk_device_get(enum disk_device_type type);

/**
 * @brief Parse disk device type from string
 *
 * @param str Device type string (e.g., "emmc", "flash")
 * @return Disk device type, or DISK_DEVICE_TYPE_UNKNOWN if invalid
 */
enum disk_device_type disk_device_type_from_string(const char *str);

/**
 * @brief Convert disk device type to string
 *
 * @param type Disk device type
 * @return Device type string
 */
const char *disk_device_type_to_string(enum disk_device_type type);

/**
 * @brief Parse size string with K/M/G suffix
 *
 * @param str Size string (e.g., "1K", "2M", "1G")
 * @param size Pointer to store parsed size
 * @return 0 on success, negative errno on error
 */
int disk_parse_size(const char *str, size_t *size);

#endif /* ZEPHYR_SUBSYS_DISK_DISK_TOOLS_DISK_DEVICE_H_ */
