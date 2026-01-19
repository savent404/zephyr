/*
 * Copyright (c) 2026 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "disk_device.h"
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

LOG_MODULE_REGISTER(disk_tools_shell, CONFIG_DISK_TOOLS_LOG_LEVEL);

/* External device getter functions */
extern const struct disk_device *disk_device_get_emmc(void);

/* Hexdump configuration */
#define HEXDUMP_BYTES_PER_LINE 16

/**
 * @brief Get disk device by type
 */
const struct disk_device *disk_device_get(enum disk_device_type type)
{
	switch (type) {
	case DISK_DEVICE_TYPE_EMMC:
		return disk_device_get_emmc();
	default:
		return NULL;
	}
}

/**
 * @brief Parse disk device type from string
 */
enum disk_device_type disk_device_type_from_string(const char *str)
{
	if (strcmp(str, "emmc") == 0) {
		return DISK_DEVICE_TYPE_EMMC;
	} else if (strcmp(str, "flash") == 0) {
		return DISK_DEVICE_TYPE_FLASH;
	} else if (strcmp(str, "sd") == 0) {
		return DISK_DEVICE_TYPE_SD;
	} else if (strcmp(str, "ram") == 0) {
		return DISK_DEVICE_TYPE_RAM;
	}

	return DISK_DEVICE_TYPE_UNKNOWN;
}

/**
 * @brief Convert disk device type to string
 */
const char *disk_device_type_to_string(enum disk_device_type type)
{
	switch (type) {
	case DISK_DEVICE_TYPE_EMMC:
		return "emmc";
	case DISK_DEVICE_TYPE_FLASH:
		return "flash";
	case DISK_DEVICE_TYPE_SD:
		return "sd";
	case DISK_DEVICE_TYPE_RAM:
		return "ram";
	default:
		return "unknown";
	}
}

/**
 * @brief Parse size string with K/M/G suffix
 */
int disk_parse_size(const char *str, size_t *size)
{
	char *endptr;
	unsigned long val;
	size_t multiplier = 1;

	if (str == NULL || size == NULL) {
		return -EINVAL;
	}

	val = strtoul(str, &endptr, 0);

	if (endptr == str) {
		return -EINVAL;
	}

	/* Check for size suffix */
	if (*endptr != '\0') {
		char suffix = toupper(*endptr);

		switch (suffix) {
		case 'K':
			multiplier = 1024;
			break;
		case 'M':
			multiplier = 1024 * 1024;
			break;
		case 'G':
			multiplier = 1024 * 1024 * 1024;
			break;
		default:
			return -EINVAL;
		}
	}

	*size = val * multiplier;
	return 0;
}

/**
 * @brief Print data in hexdump format
 */
static void hexdump_line(const struct shell *sh, size_t offset, const uint8_t *data, size_t len)
{
	char hex_str[HEXDUMP_BYTES_PER_LINE * 3 + 2]; /* +2 for extra space and null */
	char ascii_str[HEXDUMP_BYTES_PER_LINE + 1];
	size_t i;
	size_t hex_pos = 0;

	/* Print offset */
	shell_fprintf(sh, SHELL_NORMAL, "%08zx  ", offset);

	/* Build hex and ASCII strings */
	for (i = 0; i < HEXDUMP_BYTES_PER_LINE; i++) {
		if (i < len) {
			snprintf(&hex_str[hex_pos], 4, "%02x ", data[i]);
			ascii_str[i] = isprint(data[i]) ? data[i] : '.';
		} else {
			snprintf(&hex_str[hex_pos], 4, "   ");
			ascii_str[i] = ' ';
		}
		hex_pos += 3;

		/* Add extra space after the 8th byte */
		if (i == 7) {
			hex_str[hex_pos] = ' ';
			hex_pos++;
		}
	}
	hex_str[hex_pos] = '\0';
	ascii_str[HEXDUMP_BYTES_PER_LINE] = '\0';

	/* Print hex and ASCII */
	shell_fprintf(sh, SHELL_NORMAL, "%s |%s|\n", hex_str, ascii_str);
}

/**
 * @brief Dump disk data in hexdump format
 */
static int cmd_disk_dump(const struct shell *sh, size_t argc, char **argv)
{
	enum disk_device_type type;
	const struct disk_device *dev;
	size_t start_offset;
	size_t length;
	size_t offset;
	size_t remaining;
	uint8_t buf[CONFIG_DISK_TOOLS_BUFFER_SIZE];
	int ret;

	/* Parse arguments */
	if (argc != 4) {
		shell_error(sh, "Usage: disk dump <device> <start> <len>");
		shell_error(sh, "Example: disk dump emmc 0 512");
		shell_error(sh, "         disk dump emmc 0x1000 1K");
		return -EINVAL;
	}

	/* Parse device type */
	type = disk_device_type_from_string(argv[1]);
	if (type == DISK_DEVICE_TYPE_UNKNOWN) {
		shell_error(sh, "Unknown device type: %s", argv[1]);
		shell_error(sh, "Supported: emmc");
		return -EINVAL;
	}

	/* Get device */
	dev = disk_device_get(type);
	if (dev == NULL) {
		shell_error(sh, "Device not available: %s", argv[1]);
		return -ENODEV;
	}

	/* Parse start offset */
	ret = disk_parse_size(argv[2], &start_offset);
	if (ret != 0) {
		shell_error(sh, "Invalid start offset: %s", argv[2]);
		return ret;
	}

	/* Parse length */
	ret = disk_parse_size(argv[3], &length);
	if (ret != 0) {
		shell_error(sh, "Invalid length: %s", argv[3]);
		return ret;
	}

	/* Validate length */
	if (length == 0) {
		shell_error(sh, "Length must be greater than 0");
		return -EINVAL;
	}

	if (length > CONFIG_DISK_TOOLS_MAX_TRANSFER_SIZE) {
		shell_error(sh, "Length too large (max: %lld bytes)",
			    (long long)CONFIG_DISK_TOOLS_MAX_TRANSFER_SIZE);
		return -EINVAL;
	}

	/* Initialize device */
	if (dev->ops->init) {
		ret = dev->ops->init();
		if (ret != 0) {
			shell_error(sh, "Failed to initialize device: %d", ret);
			return ret;
		}
	}

	shell_print(sh, "Dumping %zu bytes from %s at offset 0x%zx:", length, dev->ops->get_name(),
		    start_offset);
	shell_print(sh, "");

	/* Read and dump data */
	offset = start_offset;
	remaining = length;

	while (remaining > 0) {
		size_t read_size = MIN(remaining, sizeof(buf));

		ret = dev->ops->read(offset, buf, read_size);
		if (ret < 0) {
			shell_error(sh, "Read error at offset 0x%zx: %d", offset, ret);
			return ret;
		}

		if (ret == 0) {
			shell_error(sh, "Unexpected end of data at offset 0x%zx", offset);
			break;
		}

		/* Print hexdump */
		for (size_t i = 0; i < ret; i += HEXDUMP_BYTES_PER_LINE) {
			size_t line_len = MIN(ret - i, HEXDUMP_BYTES_PER_LINE);

			hexdump_line(sh, offset + i, &buf[i], line_len);
		}

		offset += ret;
		remaining -= ret;
	}

	shell_print(sh, "");
	shell_print(sh, "Dumped %zu bytes", length - remaining);

	return 0;
}

#ifdef CONFIG_DISK_TOOLS_NETWORK
/* Forward declaration for network transfer */
extern int disk_tools_network_transfer(const struct disk_device *dev, size_t offset, size_t length);

/**
 * @brief Transfer disk data over network
 */
static int cmd_disk_transfer(const struct shell *sh, size_t argc, char **argv)
{
	enum disk_device_type type;
	const struct disk_device *dev;
	size_t start_offset;
	size_t length;
	int ret;

	/* Parse arguments */
	if (argc != 4) {
		shell_error(sh, "Usage: disk transf <device> <start> <len>");
		shell_error(sh, "Example: disk transf emmc 0 1M");
		shell_error(sh, "         disk transf emmc 0x1000 512K");
		return -EINVAL;
	}

	/* Parse device type */
	type = disk_device_type_from_string(argv[1]);
	if (type == DISK_DEVICE_TYPE_UNKNOWN) {
		shell_error(sh, "Unknown device type: %s", argv[1]);
		shell_error(sh, "Supported: emmc");
		return -EINVAL;
	}

	/* Get device */
	dev = disk_device_get(type);
	if (dev == NULL) {
		shell_error(sh, "Device not available: %s", argv[1]);
		return -ENODEV;
	}

	/* Parse start offset */
	ret = disk_parse_size(argv[2], &start_offset);
	if (ret != 0) {
		shell_error(sh, "Invalid start offset: %s", argv[2]);
		return ret;
	}

	/* Parse length */
	ret = disk_parse_size(argv[3], &length);
	if (ret != 0) {
		shell_error(sh, "Invalid length: %s", argv[3]);
		return ret;
	}

	/* Validate length */
	if (length == 0) {
		shell_error(sh, "Length must be greater than 0");
		return -EINVAL;
	}

	if (length > CONFIG_DISK_TOOLS_MAX_TRANSFER_SIZE) {
		shell_error(sh, "Length too large (max: %lld bytes)",
			    (long long)CONFIG_DISK_TOOLS_MAX_TRANSFER_SIZE);
		return -EINVAL;
	}

	/* Initialize device */
	if (dev->ops->init) {
		ret = dev->ops->init();
		if (ret != 0) {
			shell_error(sh, "Failed to initialize device: %d", ret);
			return ret;
		}
	}

	shell_print(sh, "Starting network transfer:");
	shell_print(sh, "  Device: %s", dev->ops->get_name());
	shell_print(sh, "  Offset: 0x%zx", start_offset);
	shell_print(sh, "  Length: %zu bytes (%.2f KB)", length, length / 1024.0);
	shell_print(sh, "");

	/* Start network transfer */
	ret = disk_tools_network_transfer(dev, start_offset, length);
	if (ret != 0) {
		shell_error(sh, "Network transfer failed: %d", ret);
		return ret;
	}

	return 0;
}
#endif /* CONFIG_DISK_TOOLS_NETWORK */

/* Define shell commands */
SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_disk,
	SHELL_CMD_ARG(dump, NULL,
		      "Dump disk data in hexdump format\n"
		      "Usage: disk dump <device> <start> <len>\n"
		      "  device: emmc, flash, etc.\n"
		      "  start:  start offset (supports K/M/G suffix)\n"
		      "  len:    length to dump (supports K/M/G suffix)\n"
		      "Example: disk dump emmc 0 512\n"
		      "         disk dump emmc 0x1000 1K",
		      cmd_disk_dump, 4, 0),
#ifdef CONFIG_DISK_TOOLS_NETWORK
	SHELL_CMD_ARG(transf, NULL,
		      "Transfer disk data over network\n"
		      "Usage: disk transf <device> <start> <len>\n"
		      "  device: emmc, flash, etc.\n"
		      "  start:  start offset (supports K/M/G suffix)\n"
		      "  len:    length to transfer (supports K/M/G suffix)\n"
		      "Example: disk transf emmc 0 1M\n"
		      "         disk transf emmc 0x1000 512K",
		      cmd_disk_transfer, 4, 0),
#endif
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(disk, &sub_disk, "Disk tools commands", NULL);
