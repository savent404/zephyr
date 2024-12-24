/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/sd/sd.h>
#include <zephyr/sd/sdmmc.h>
#include <zephyr/sd/mmc.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef int (*read_blocks_t)(struct sd_card *card, uint8_t *rbuf, uint32_t start_block,
			     uint32_t num_blocks);
typedef int (*write_blocks_t)(struct sd_card *card, const uint8_t *wbuf, uint32_t start_block,
			      uint32_t num_blocks);

static int sdhc_test_inst(const struct device *dev);
static int write_validate(struct sd_card *card, uint32_t start_block, uint32_t num_blocks,
			  read_blocks_t read_, write_blocks_t write_);
static int benchmark(struct sd_card *card, uint32_t start_block, uint32_t num_blocks,
		     uint32_t chunk, read_blocks_t read_, write_blocks_t write_);

#define MMC_DEV (DEVICE_DT_GET(DT_ALIAS(mmc)))

int main(void)
{
#ifdef CONFIG_SAMPLE_DO_OUTPUT
	printk("Hello World from minimal!\n");
#endif

	sdhc_test_inst(MMC_DEV);

	k_sleep(K_FOREVER);

	return 0;
}

static int xxd_dump(uint8_t *data, unsigned int size)
{
	unsigned int i;

	for (i = 0; i < size; i++) {
		if (i % 16 == 0) {
			printk("\n%08x: ", i);
		}
		printk("%02x ", data[i]);
	}

	printk("\n");

	return 0;
}

const char *voltage_type(enum sd_voltage voltage)
{
	switch (voltage) {
	case SD_VOL_3_3_V:
		return "3.3V";
	case SD_VOL_3_0_V:
		return "3.0V";
	case SD_VOL_1_8_V:
		return "1.8V";
	case SD_VOL_1_2_V:
		return "1.2V";
	default:
		return "Unknown";
	}
}

const char *timing_type(enum sdhc_timing_mode timing)
{
	switch (timing) {
	case SDHC_TIMING_LEGACY:
		return "Legacy";
	case SDHC_TIMING_HS:
		return "High speed";
	case SDHC_TIMING_SDR12:
		return "SDR12";
	case SDHC_TIMING_SDR25:
		return "SDR25";
	case SDHC_TIMING_SDR50:
		return "SDR50";
	case SDHC_TIMING_SDR104:
		return "SDR104";
	case SDHC_TIMING_DDR50:
		return "DDR50";
	case SDHC_TIMING_DDR52:
		return "DDR52";
	case SDHC_TIMING_HS200:
		return "HS200";
	case SDHC_TIMING_HS400:
		return "HS400";
	default:
		return "Unknown";
	}
}

static int sdhc_test_inst(const struct device *dev)
{
	const char *dev_name = dev->name;
	struct sd_card card = {0};
	uint32_t blocks_128M = 2 * 1024 * 128;
	uint32_t blocks_1M = 2 * 1024;
	uint32_t chunk_128K = 2 * 128;

	card.bus_width = SDHC_BUS_WIDTH4BIT;

	printk("Testing %s\n", dev_name);

	if (!device_is_ready(dev)) {
		printk("Device is not ready\n");
		return -EIO;
	}

	if (!sd_is_card_present(dev)) {
		printk("No card present, but let's try anyway\n");
	}

	if (sd_init(dev, &card)) {
		printk("Failed to initialize card\n");
		return -EIO;
	}

	printk("Card initialized\n");
	printk("Card voltage: %s\n", voltage_type(card.bus_io.signal_voltage));
	printk("Card block size: %d\n", card.block_size);
	printk("Card block count: %dK\n", card.block_count / 1024);
	printk("Card capacity: %uM\n",
	       (unsigned int)card.block_size * card.block_count / 1024 / 1024);
	printk("Card timing: %s\n", timing_type(card.bus_io.timing));
	printk("Card driver type: %d\n", card.bus_io.driver_type);
	printk("Card frequency: %dMHz\n", card.bus_io.clock / 1000000);
	printk("Card bus_width: %d\n", card.bus_io.bus_width);

	switch (card.type) {
	case CARD_SDMMC:
		printk("Card type: SDMMC\n");
		write_validate(&card, 0, blocks_1M, sdmmc_read_blocks, sdmmc_write_blocks);
		benchmark(&card, 0, blocks_128M, chunk_128K, sdmmc_read_blocks, sdmmc_write_blocks);
		break;
	case CARD_MMC:
		printk("Card type: MMC\n");
		write_validate(&card, 0, blocks_1M, mmc_read_blocks, mmc_write_blocks);
		benchmark(&card, 0, blocks_128M, chunk_128K, mmc_read_blocks, mmc_write_blocks);
		break;
	default:
		printk("Card type: Unknown\n");
		break;
	}

	return 0;
}

static int benchmark(struct sd_card *card, uint32_t start_block, uint32_t num_blocks,
		     uint32_t block_chunk, read_blocks_t read_, write_blocks_t write_)
{
	uint64_t rd_acc = 0, wr_acc = 0;
	uint8_t *data = malloc(block_chunk * 512);

	printk("Benchmark...\n");

	for (unsigned int chunk = 0; chunk * block_chunk < num_blocks; chunk++) {
		uint32_t start = k_cycle_get_32();

		if (read_(card, data, start_block + chunk * block_chunk, block_chunk)) {
			printk("Failed to read block\n");
			goto err;
		}
		uint32_t end = k_cycle_get_32();
		uint32_t diff = end - start;

		rd_acc += diff;
		printk("Read block %d-%d in %d cycles, average %.2f MB/s\n",
		       start_block + chunk * block_chunk, start_block + (chunk + 1) * block_chunk,
		       diff,
		       (double)block_chunk * 512 / diff * sys_clock_hw_cycles_per_sec() / 1024 /
			       1024);
	}

	for (unsigned int chunk = 0; chunk * block_chunk < num_blocks; chunk++) {
		uint32_t start = k_cycle_get_32();

		if (sdmmc_write_blocks(card, data, start_block + chunk * block_chunk,
				       block_chunk)) {
			printk("Failed to write block\n");
			goto err;
		}
		uint32_t end = k_cycle_get_32();
		uint32_t diff = end - start;

		wr_acc += diff;
		printk("Write block %d-%d in %d cycles, average %.2f MB/s\n",
		       start_block + chunk * block_chunk, start_block + (chunk + 1) * block_chunk,
		       diff,
		       (double)block_chunk * 512 / diff * sys_clock_hw_cycles_per_sec() / 1024 /
			       1024);
	}

	free(data);
	printk("Read %d blocks in %lld cycles, average %.2f MB/s\n", num_blocks, rd_acc,
	       (double)num_blocks * 512 / rd_acc * sys_clock_hw_cycles_per_sec() / 1024 / 1024);
	printk("Write %d blocks in %lld cycles, average %.2f MB/s\n", num_blocks, wr_acc,
	       (double)num_blocks * 512 / wr_acc * sys_clock_hw_cycles_per_sec() / 1024 / 1024);
	printk("Benchmark...done\n");
	return 0;
err:
	free(data);
	printk("Benchmark fatal error\n");
	printk("Benchmark...done\n");
	return -EIO;
}

static int write_validate(struct sd_card *card, uint32_t start_block, uint32_t num_blocks,
			  int (*read_)(struct sd_card *card, uint8_t *rbuf, uint32_t start_block,
				       uint32_t num_blocks),
			  int (*write_)(struct sd_card *card, const uint8_t *wbuf,
					uint32_t start_block, uint32_t num_blocks))
{
	static uint8_t data[1024]; /* Give stack some space !*/
	static uint8_t r_data[1024];

	printk("Write validate...\n");

	if (num_blocks % 2 != 0) {
		printk("Number of blocks must be even\n");
		return -EINVAL;
	}

	for (unsigned int i = 0; i < 1024; i++) {
		data[i] = i;
	}

	/* single write validate */
	for (uint32_t i = 0; i < num_blocks; i++) {
		uint32_t block = start_block + i;

		if (write_(card, data, block, 1)) {
			printk("Failed to write block %d\n", block);
			return -EIO;
		}

		if (read_(card, r_data, block, 1)) {
			printk("Failed to read block %d\n", block);
			return -EIO;
		}

		if (memcmp(data, r_data, 512)) {
			printk("Block %d data mismatch\n", block);
			xxd_dump(data, 512);
			xxd_dump(r_data, 512);
			return -EIO;
		}
	}

	/* multi write validate */
	for (uint32_t i = 0; i < num_blocks; i += 2) {
		uint32_t block = start_block + i;

		if (write_(card, data, block, 2)) {
			printk("Failed to write block %d\n", block);
			return -EIO;
		}

		if (read_(card, r_data, block, 2)) {
			printk("Failed to read block %d\n", block);
			return -EIO;
		}

		if (memcmp(data, r_data, 1024)) {
			printk("Block %d data mismatch\n", block);
			xxd_dump(data, 1024);
			xxd_dump(r_data, 1024);
			return -EIO;
		}
	}

	printk("Write validate...done\n");

	return 0;
}
