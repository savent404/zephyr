/**
 * Copyright (c) 2024 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/printk.h>

/*  Define the SPI bus to use  */
#define SPI_BUS DEVICE_DT_GET(DT_NODELABEL(spi1))

/*  Define TPM command and expected response  */
#define TPM_COMMAND_BUFFER    {0xc3, 0xd4, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00}
#define TPM_EXPECTED_RESPONSE {0x80, 0x00, 0x00, 0x01, 0x4e, 0x1b, 0x03, 0x06}

int main(void)
{
	const struct device *spi_dev = SPI_BUS;
	/*  TPM command buffer  */
	uint8_t tx_buf[] = TPM_COMMAND_BUFFER;
	/*  Response buffer  */
	uint8_t rx_buf[8];

	struct spi_config spi_cfg = {
		.frequency = 1000000,
		.operation = SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8),
		.slave = 0,
		.cs = {},
	};

	struct spi_buf tx_bufs[] = {
		{
			.buf = tx_buf,
			.len = sizeof(tx_buf),
		},
	};
	struct spi_buf rx_bufs[] = {
		{
			.buf = rx_buf,
			.len = sizeof(rx_buf),
		},
	};
	struct spi_buf_set tx = {
		.buffers = tx_bufs,
		.count = ARRAY_SIZE(tx_bufs),
	};
	struct spi_buf_set rx = {
		.buffers = rx_bufs,
		.count = ARRAY_SIZE(rx_bufs),
	};

	if (!spi_dev) {
		printk("SPI device not found\n");
		return 0;
	}

	if (!device_is_ready(spi_dev)) {
		printk("%s: device not ready.\n", spi_dev->name);
		return 0;
	}

	printk("\nStarting TPM probe...\n");
	printk("SPI device: %s\n", spi_dev->name);
	printk("SPI configuration:\n");
	printk("  - Frequency: %d Hz\n", spi_cfg.frequency);
	printk("  - Mode: %s\n",
	       (spi_cfg.operation & SPI_MODE_CPOL)
		       ? ((spi_cfg.operation & SPI_MODE_CPHA) ? "MODE3" : "MODE2")
		       : ((spi_cfg.operation & SPI_MODE_CPHA) ? "MODE1" : "MODE0"));
	printk("  - Word size: %d bits\n", SPI_WORD_SIZE_GET(spi_cfg.operation));
	printk("  - Slave address: %d\n", spi_cfg.slave);

	/*  Perform SPI transaction  */
	int ret = spi_transceive(spi_dev, &spi_cfg, &tx, &rx);

	if (ret) {
		printk("SPI transaction failed: %d\n", ret);
		return 0;
	}

	/*  Print received data  */
	printk("Received TPM response: ");
	for (int i = 0; i < sizeof(rx_buf); i++) {
		printk("%02x ", rx_buf[i]);
	}
	printk("\n");

	/*  Check if the response matches expected values  */
	uint8_t expected[] = TPM_EXPECTED_RESPONSE;
	bool match = true;

	for (int i = 0; i < sizeof(expected); i++) {
		if (rx_buf[i] != expected[i]) {
			match = false;
			break;
		}
	}

	if (match) {
		printk("Success: Received response matches expected values\n");
	} else {
		printk("Warning: Response does not match expected values\n");
		printk("Expected: ");
		for (int i = 0; i < sizeof(expected); i++) {
			printk("%02x ", expected[i]);
		}
		printk("\n");
	}

	printk("TPM probe completed.\n");

	return 0;
}
