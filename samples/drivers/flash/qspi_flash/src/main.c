/**
 * Copyright (c) 2024 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(qspi_flash);

#define FLASH_DEV_NAME   DT_ALIAS(flash)
#define TEST_BLOCK_SIZE  4096
#define START_OFFSET     0xff0000
#define TEST_REGION_SIZE 0xf000
#define TEST_ITERATIONS  (2)

static uint8_t write_buf[TEST_BLOCK_SIZE];
static uint8_t read_buf[TEST_BLOCK_SIZE];

static void fill_test_pattern(uint8_t *buf, uint32_t offset);
static int verify_block(const uint8_t *expected, const uint8_t *actual, uint32_t offset);
static void print_test_info(void);

int main(void)
{
	const struct device *flash_dev;
	const struct cad_qspi_params *flash_params;
	int ret;
	uint32_t current_offset;
	uint32_t test_count = 0;
	uint32_t error_count = 0;
	uint32_t total_blocks;

	print_test_info();

	flash_dev = DEVICE_DT_GET(FLASH_DEV_NAME);
	if (!device_is_ready(flash_dev)) {
		LOG_ERR("Flash device not ready");
		return -1;
	}
	total_blocks = TEST_REGION_SIZE / TEST_BLOCK_SIZE;

	printk("\nFlash Device: %s\n", flash_dev->name);
	flash_params = (const struct cad_qspi_params *)flash_dev->data;
	printk("Flash Test Starting...\n\n");
	printk("Region: 0x%x - 0x%x (%d blocks)\n", START_OFFSET, START_OFFSET + TEST_REGION_SIZE,
	       total_blocks);

	while (test_count < TEST_ITERATIONS) {
		current_offset = START_OFFSET;
		int block_count = 0;

		while (current_offset < (START_OFFSET + TEST_REGION_SIZE)) {
			printk("current test region: 0x%x - 0x%x\n", current_offset,
			       current_offset + TEST_BLOCK_SIZE);

			ret = flash_erase(flash_dev, current_offset, TEST_BLOCK_SIZE);
			if (ret != 0) {
				LOG_ERR("\nErase failed at 0x%x", current_offset);
				error_count++;
				goto next_block;
			}

			fill_test_pattern(write_buf, block_count);
			ret = flash_write(flash_dev, current_offset, write_buf, TEST_BLOCK_SIZE);
			if (ret != 0) {
				LOG_ERR("\nWrite failed at 0x%x", current_offset);
				error_count++;
				goto next_block;
			}

			ret = flash_read(flash_dev, current_offset, read_buf, TEST_BLOCK_SIZE);
			if (ret != 0) {
				LOG_ERR("\nRead failed at 0x%x", current_offset);
				error_count++;
				goto next_block;
			}

			if (verify_block(write_buf, read_buf, current_offset)) {
				error_count++;
			}

next_block:
			current_offset += TEST_BLOCK_SIZE;
			block_count++;
		}

		test_count++;
		printk("\n");
		printk("Iteration %d/%d complete - %d errors\n", test_count, TEST_ITERATIONS,
		       error_count);
	}

	printk("\nTotal Errors: %d\n", error_count);
	printk("Test Complete - %s\n", error_count ? "FAILED" : "PASSED");

	return error_count ? -1 : 0;
}

static void fill_test_pattern(uint8_t *buf, uint32_t offset)
{
	for (int i = 0; i < TEST_BLOCK_SIZE; i++) {
		buf[i] = (offset * i) & 0xFF;
	}
}

static int verify_block(const uint8_t *expected, const uint8_t *actual, uint32_t offset)
{
	for (int i = 0; i < TEST_BLOCK_SIZE; i++) {
		if (expected[i] != actual[i]) {
			LOG_ERR("Data mismatch at 0x%x[%d]: exp 0x%02x, got 0x%02x", offset, i,
				expected[i], actual[i]);
			return -1;
		}
	}
	return 0;
}

static void print_test_info(void)
{
	printk("=== QSPI Flash Test Description ===\n");
	printk("Test Mechanism:\n");
	printk("1. Sequential Block Testing\n");
	printk("   - Start Address: 0x%x\n", START_OFFSET);
	printk("   - Test Region: %d KB\n", TEST_REGION_SIZE / 1024);
	printk("   - Block Size: %d bytes\n", TEST_BLOCK_SIZE);

	printk("2. Operation Sequence:\n");
	printk("   - Erase block\n");
	printk("   - Write test pattern\n");
	printk("   - Read back data\n");
	printk("   - Verify data integrity\n");

	printk("3. Test Iterations: %d\n", TEST_ITERATIONS);
	printk("================================\n");
}
