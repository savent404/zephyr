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

static int sdhc_test_inst(const struct device *dev);
static int benchmark(struct sd_card *card, uint32_t start_block, uint32_t num_blocks,
		     int (*read_)(struct sd_card *card, uint8_t *rbuf, uint32_t start_block,
				  uint32_t num_blocks));

int main(void)
{
#ifdef CONFIG_SAMPLE_DO_OUTPUT
	printk("Hello World from minimal!\n");
#endif

	sdhc_test_inst(DEVICE_DT_GET(DT_NODELABEL(mmc0)));

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
static int sdhc_test_inst(const struct device *dev)
{
	const char *dev_name = dev->name;
	static uint8_t data[1024]; /* Give stack some space !*/
	struct sd_card card = {0};

	card.bus_io.clock = SD_CLOCK_50MHZ;
	card.bus_io.bus_mode = SDHC_BUSMODE_PUSHPULL;
	card.bus_io.power_mode = SDHC_POWER_ON;
	card.bus_io.bus_width = SDHC_BUS_WIDTH4BIT;
	card.bus_io.timing = SDHC_TIMING_LEGACY;
	card.bus_io.driver_type = SD_DRIVER_TYPE_B;
	card.bus_io.signal_voltage = SD_VOL_3_3_V;

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
	printk("Card voltage: %d\n", card.card_voltage);
	printk("Card block size: %d\n", card.block_size);
	printk("Card block count: %d\n", card.block_count);
	printk("Card capacity: %d\n", card.block_size * card.block_count);
	printk("Card version: %d\n", card.sd_version);
	printk("Card speed: %d\n", card.card_speed);
	printk("Card type: %d\n", card.type);
	printk("Card bus_width: %d\n", card.bus_width);

	switch (card.type) {
	case CARD_SDMMC:
		printk("Card type: SDMMC\n");
		if (sdmmc_read_blocks(&card, data, 0, 2)) {
			printk("Failed to read block\n");
		} else {
			printk("Read block 0:\n");
			xxd_dump(data, 1024);
		}

		benchmark(&card, 0, card.block_count, sdmmc_read_blocks);
		break;
	case CARD_MMC:
		printk("Card type: MMC\n");
		if (mmc_read_blocks(&card, data, 0, 2)) {
			printk("Failed to read block\n");
		} else {
			printk("Read block 0:\n");
			xxd_dump(data, 1024);
		}

		benchmark(&card, 0, card.block_count, mmc_read_blocks);
		break;
	default:
		printk("Card type: Unknown\n");
		break;
	}

	return 0;
}

static int benchmark(struct sd_card *card, uint32_t start_block, uint32_t num_blocks,
		     int (*read_)(struct sd_card *card, uint8_t *rbuf, uint32_t start_block,
				  uint32_t num_blocks))
{
	uint64_t acc = 0;
#define block_chunk (2 * 128) /* 128KB per chunk */
	static uint8_t data[512 * block_chunk];

	for (unsigned int chunk = 0; chunk * block_chunk < num_blocks; chunk++) {
		uint32_t start = k_cycle_get_32();

		if (read_(card, data, start_block + chunk * block_chunk, block_chunk)) {
			printk("Failed to read block\n");
			return -EIO;
		}
		uint32_t end = k_cycle_get_32();
		uint32_t diff = end - start;

		acc += diff;
		printk("Read block %d-%d in %d cycles, average %.2f MB/s\n",
		       start_block + chunk * block_chunk, start_block + (chunk + 1) * block_chunk,
		       diff,
		       (double)block_chunk * 512 / diff * sys_clock_hw_cycles_per_sec() / 1024 /
			       1024);
	}

	printk("Read %d blocks in %lld cycles, average %.2f MB/s\n", num_blocks, acc,
	       (double)num_blocks * 512 / acc * sys_clock_hw_cycles_per_sec() / 1024 / 1024);
	return 0;
}
