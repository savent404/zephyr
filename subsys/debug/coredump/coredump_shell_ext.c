/*
 * Copyright (c) 2026 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/debug/coredump.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/devicetree.h>

#ifdef CONFIG_DEBUG_COREDUMP_BACKEND_EMMC
#define EMMC_PARTITION      coredump_partition
#define EMMC_PARTITION_NODE DT_NODELABEL(EMMC_PARTITION)
#endif

LOG_MODULE_REGISTER(coredump_shell_ext, LOG_LEVEL_INF);

/**
 * @brief Shell command to dump coredump content
 */
static int cmd_coredump_dump(const struct shell *sh, size_t argc, char **argv)
{
	int ret;
	int size;
	uint8_t buf[16];
	int offset = 0;
	struct coredump_cmd_copy_arg copy_arg;

	/* Check if coredump exists */
	size = coredump_query(COREDUMP_QUERY_GET_STORED_DUMP_SIZE, NULL);
	if (size < 0) {
		shell_error(sh, "Failed to query coredump size: %d", size);
		return size;
	}

	if (size == 0) {
		shell_info(sh, "No stored coredump found");
		return 0;
	}

	shell_print(sh, "Coredump size: %d bytes", size);
	shell_print(sh, "");
	shell_print(sh, "Offset   00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F  ASCII");
	shell_print(sh, "--------------------------------------------------------------------");

	/* Dump data in hex format */
	while (offset < size) {
		copy_arg.offset = offset;
		copy_arg.buffer = buf;
		copy_arg.length = sizeof(buf);

		ret = coredump_cmd(COREDUMP_CMD_COPY_STORED_DUMP, &copy_arg);
		if (ret < 0) {
			shell_error(sh, "Failed to read coredump at offset %d: %d", offset, ret);
			break;
		}

		if (ret == 0) {
			break;
		}

		int read_len = ret;

		/* Print offset */
		shell_fprintf(sh, SHELL_NORMAL, "%08X ", offset);

		/* Print hex bytes */
		for (int i = 0; i < 16; i++) {
			if (i < read_len) {
				shell_fprintf(sh, SHELL_NORMAL, "%02X ", buf[i]);
			} else {
				shell_fprintf(sh, SHELL_NORMAL, "   ");
			}
		}

		shell_fprintf(sh, SHELL_NORMAL, " ");

		/* Print ASCII */
		for (int i = 0; i < read_len; i++) {
			char c = buf[i];

			if (c >= 32 && c <= 126) {
				shell_fprintf(sh, SHELL_NORMAL, "%c", c);
			} else {
				shell_fprintf(sh, SHELL_NORMAL, ".");
			}
		}

		shell_fprintf(sh, SHELL_NORMAL, "\n");

		offset += read_len;
	}

	shell_print(sh, "");
	shell_print(sh, "Total: %d bytes dumped", offset);

	return 0;
}

/**
 * @brief Shell command to dump raw coredump header
 */
static int cmd_coredump_dump_header(const struct shell *sh, size_t argc, char **argv)
{
	int ret;
	uint8_t buf[64];
	struct coredump_cmd_copy_arg copy_arg;

	shell_print(sh, "Reading coredump DATA header (first 64 bytes after eMMC header):");
	shell_print(sh, "Note: This shows the Zephyr coredump data, not the eMMC backend header");
	shell_print(sh, "");

	copy_arg.offset = 0;
	copy_arg.buffer = buf;
	copy_arg.length = sizeof(buf);

	ret = coredump_cmd(COREDUMP_CMD_COPY_STORED_DUMP, &copy_arg);
	if (ret < 0) {
		shell_error(sh, "Failed to read coredump header: %d", ret);
		return ret;
	}

	if (ret == 0) {
		shell_info(sh, "No stored coredump found");
		return 0;
	}

	shell_print(sh, "Offset   00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F");
	shell_print(sh, "----------------------------------------------------------------");

	for (int i = 0; i < 64; i += 16) {
		shell_fprintf(sh, SHELL_NORMAL, "%08X ", i);
		for (int j = 0; j < 16; j++) {
			if (i + j < ret) {
				shell_fprintf(sh, SHELL_NORMAL, "%02X ", buf[i + j]);
			} else {
				shell_fprintf(sh, SHELL_NORMAL, "   ");
			}
		}
		shell_fprintf(sh, SHELL_NORMAL, "\n");
	}

	shell_print(sh, "");
	shell_print(sh, "Zephyr Coredump Header interpretation:");
	shell_print(sh, "  ID:      %c%c (0x%02X 0x%02X) - Should be 'ZE' for Zephyr", buf[0],
		    buf[1], buf[0], buf[1]);
	shell_print(sh, "  Version: %u", *(uint16_t *)&buf[2]);
	shell_print(sh, "  Reason:  0x%04X", *(uint16_t *)&buf[4]);
	shell_print(sh, "  Ptr size bits: %u", buf[6]);
	shell_print(sh, "  Target code: 0x%04X", *(uint16_t *)&buf[7]);

	return 0;
}

#ifdef CONFIG_DEBUG_COREDUMP_BACKEND_EMMC
/* External function for debugging */
extern int coredump_emmc_read_raw_sector(uint32_t sector, uint8_t *buf, uint32_t count);

/**
 * @brief Shell command to dump raw eMMC backend header (sector 0)
 */
static int cmd_coredump_dump_emmc_header(const struct shell *sh, size_t argc, char **argv)
{
	int ret;
	uint8_t buf[512];

	shell_print(sh, "Reading eMMC backend header (sector 0, 512 bytes):");
	shell_print(sh, "");

	ret = coredump_emmc_read_raw_sector(0, buf, 1);
	if (ret < 0) {
		shell_error(sh, "Failed to read sector 0: %d", ret);
		return ret;
	}

	shell_print(sh, "Offset   00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F");
	shell_print(sh, "----------------------------------------------------------------");

	for (int i = 0; i < 128; i += 16) {
		shell_fprintf(sh, SHELL_NORMAL, "%08X ", i);
		for (int j = 0; j < 16; j++) {
			shell_fprintf(sh, SHELL_NORMAL, "%02X ", buf[i + j]);
		}
		shell_fprintf(sh, SHELL_NORMAL, "\n");
	}

	shell_print(sh, "");
	shell_print(sh, "eMMC Backend Header interpretation:");
	shell_print(sh, "  ID:       %c%c (0x%02X 0x%02X) - Should be 'CD' for coredump", buf[0],
		    buf[1], buf[0], buf[1]);
	shell_print(sh, "  Version:  %u", *(uint16_t *)&buf[2]);
	shell_print(sh, "  Size:     %u bytes", *(uint32_t *)&buf[4]);
	shell_print(sh, "  Flags:    0x%04X", *(uint16_t *)&buf[8]);
	shell_print(sh, "  Checksum: 0x%04X", *(uint16_t *)&buf[10]);
	shell_print(sh, "  Error:    %d", *(int *)&buf[12]);

	return 0;
}
#endif /* CONFIG_DEBUG_COREDUMP_BACKEND_EMMC */

/**
 * @brief Shell command to get coredump info
 */
static int cmd_coredump_info(const struct shell *sh, size_t argc, char **argv)
{
	int ret;
	int size;
	int has_dump;

	/* Check if coredump exists */
	ret = coredump_query(COREDUMP_QUERY_HAS_STORED_DUMP, &has_dump);
	if (ret < 0) {
		shell_error(sh, "Failed to query coredump: %d", ret);
		return ret;
	}

	if (!has_dump) {
		shell_info(sh, "No stored coredump found");
		return 0;
	}

	/* Get size */
	size = coredump_query(COREDUMP_QUERY_GET_STORED_DUMP_SIZE, NULL);
	if (size < 0) {
		shell_error(sh, "Failed to get coredump size: %d", size);
		return size;
	}

	/* Verify */
	ret = coredump_cmd(COREDUMP_CMD_VERIFY_STORED_DUMP, NULL);

	shell_print(sh, "Coredump Information:");
	shell_print(sh, "  Status:   %s", has_dump ? "Present" : "Not found");
	shell_print(sh, "  Size:     %d bytes", size);
	shell_print(sh, "  Valid:    %s", ret == 1 ? "Yes" : (ret == 0 ? "No" : "Unknown"));

#ifdef CONFIG_DEBUG_COREDUMP_BACKEND_EMMC
	/* Calculate coredump storage location in eMMC */
	uint32_t partition_offset = DT_REG_ADDR(EMMC_PARTITION_NODE);
	/* HEADER_SECTOR_SIZE is rounded up to 512 bytes */
	uint32_t header_size = 512; /* sizeof(emmc_hdr_t) rounded to sector */
	uint32_t coredump_start = partition_offset + header_size;
	uint32_t coredump_size = size;

	shell_print(sh, "");
	shell_print(sh, "Storage Information (eMMC):");
	shell_print(sh, "  Partition offset: 0x%X (%u bytes)", partition_offset, partition_offset);
	shell_print(sh, "  Header size:      0x%X (%u bytes)", header_size, header_size);
	shell_print(sh, "  Data offset:      0x%X (%u bytes)", coredump_start, coredump_start);
	shell_print(sh, "  Data size:        0x%X (%u bytes)", coredump_size, coredump_size);
	shell_print(sh, "");
	shell_print(sh, "To export coredump via network, use:");
	shell_print(sh, "  disk transf emmc 0x%X %u", coredump_start, coredump_size);
	shell_print(sh, "");
	shell_print(sh, "Then on your PC, download with:");
	shell_print(sh, "  python scripts/coredump/coredump_client.py <device_ip> -o coredump.bin");
#endif

	return 0;
}

/* Server commands removed - use 'disk transf' command instead */

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_coredump_ext, SHELL_CMD(info, NULL, "Show coredump information", cmd_coredump_info),
	SHELL_CMD(dump, NULL, "Dump coredump content in hex", cmd_coredump_dump),
	SHELL_CMD(header, NULL, "Dump coredump data header (ZE)", cmd_coredump_dump_header),
#ifdef CONFIG_DEBUG_COREDUMP_BACKEND_EMMC
	SHELL_CMD(raw, NULL, "Dump raw eMMC backend header (CD)", cmd_coredump_dump_emmc_header),
#endif
	SHELL_SUBCMD_SET_END);

/* Use "cdump" to avoid conflict with backend's "coredump" command */
SHELL_CMD_REGISTER(cdump, &sub_coredump_ext, "Extended coredump commands", NULL);
