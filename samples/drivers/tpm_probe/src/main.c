/**
 * Copyright (c) 2025 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/device_mmio.h>

LOG_MODULE_REGISTER(tpm_probe, LOG_LEVEL_DBG);

#define SPI_NODE DT_NODELABEL(spi1)

static const struct device *spi_dev = DEVICE_DT_GET(SPI_NODE);
static struct spi_config spi_cfg = {
	.frequency = 1000000,
	.operation = SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8),
	.slave = 0,
	.cs = {},
};

/*  Define TPM command and expected response  */
#define TPM_COMMAND_BUFFER    {0xc3, 0xd4, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00}
#define TPM_EXPECTED_RESPONSE {0x80, 0x00, 0x00, 0x01, 0x4e, 0x1b, 0x03, 0x06}

static uint8_t tpm_command_buffer[] = TPM_COMMAND_BUFFER;
static uint8_t tpm_expected_response[] = TPM_EXPECTED_RESPONSE;

#define TPM_CMD_SIZE 32
#define TPM_RSP_SIZE 8
static __aligned(32) uint8_t tx_buffer[TPM_CMD_SIZE];
static __aligned(32) uint8_t rx_buffer[TPM_RSP_SIZE];

static void init_test_data(void)
{
	memset(rx_buffer, 0, sizeof(rx_buffer));
	for (int i = 0; i < TPM_CMD_SIZE; i++) {
		tx_buffer[i] = tpm_command_buffer[i % sizeof(tpm_command_buffer)];
	}
}

static int tpm_probe(void)
{
	int ret;

	struct spi_buf tx_buf = {.buf = tx_buffer, .len = TPM_CMD_SIZE};
	struct spi_buf_set tx_bufs = {.buffers = &tx_buf, .count = 1};

	struct spi_buf rx_buf = {.buf = rx_buffer, .len = TPM_RSP_SIZE};
	struct spi_buf_set rx_bufs = {.buffers = &rx_buf, .count = 1};

	if (!device_is_ready(spi_dev)) {
		LOG_ERR("SPI device not ready");
		return -ENODEV;
	}

#ifdef CONFIG_SPI_DW_DMA
	LOG_INF("Starting SPI DMA test with %d bytes (TX+RX)", TPM_CMD_SIZE + TPM_RSP_SIZE);
#else
	LOG_INF("Starting SPI test with %d bytes (TX+RX)", TPM_CMD_SIZE + TPM_RSP_SIZE);
#endif

	ret = spi_transceive(spi_dev, &spi_cfg, &tx_bufs, &rx_bufs);
	if (ret != 0) {
		LOG_ERR("SPI transceive failed: %d", ret);
		return ret;
	}

	LOG_INF("SPI transfer completed successfully");

	LOG_HEXDUMP_INF(tx_buffer, TPM_CMD_SIZE, "TX Data");
	LOG_HEXDUMP_INF(rx_buffer, TPM_RSP_SIZE, "RX Data");

	/* Check if we received any data in first 8 bytes where expected response should be */
	if (memcmp(rx_buffer, tpm_expected_response, sizeof(tpm_expected_response)) == 0) {
		LOG_INF("Data validation completed, probe tpm success.");
		return 0;
	} else {
		LOG_ERR("Data mismatch, probe tpm failed.");
		LOG_HEXDUMP_ERR(rx_buffer, sizeof(tpm_expected_response), "Expected first 8 bytes");
		LOG_HEXDUMP_ERR(tpm_expected_response, sizeof(tpm_expected_response),
				"Expected response");
		return -EIO;
	}

	return 0;
}

int main(void)
{
	LOG_INF("TPM Probe Application");
	LOG_INF("SPI device: %s", spi_dev->name);
#ifdef CONFIG_SPI_DW_DMA
	LOG_INF("SPI DMA is enabled");
#else
	LOG_INF("SPI DMA is disabled");
#endif

	init_test_data();

	int ret = tpm_probe();

	if (ret != 0) {
		LOG_ERR("SPI test failed: %d", ret);
	} else {
		LOG_INF("SPI test passed.");
	}

	while (1) {
		k_sleep(K_SECONDS(1));
	}

	/* never reach here */

	return 0;
}
