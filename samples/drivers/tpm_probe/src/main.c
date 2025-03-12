/**
 * Copyright (c) 2024 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <wolftpm/tpm2.h>

LOG_MODULE_REGISTER(tpm_probe, LOG_LEVEL_DBG);

/*  Define the SPI bus to use  */
#define SPI_BUS DEVICE_DT_GET(DT_NODELABEL(spi1))

/*  Define TPM command and expected response  */
#define TPM_COMMAND_BUFFER    {0xc3, 0xd4, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00}
#define TPM_EXPECTED_RESPONSE {0x80, 0x00, 0x00, 0x01, 0x4e, 0x1b, 0x03, 0x06}

static struct TPM2_CTX _tpm_ctx;
static struct spi_config spi_cfg = {
	.frequency = 1000000,
	.operation = SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB | SPI_WORD_SET(8),
	.slave = 0,
	.cs = {},
};
struct {
	const struct device *dev;
	struct spi_config *cfg;
} _tpm_user_ctx;
static int _tpm_io(struct TPM2_CTX *ctx, const BYTE *tx, BYTE *rx, UINT16 size, void *userCtx);

int main(void)
{
	const struct device *spi_dev = SPI_BUS;
	/*  TPM command buffer  */
	uint8_t tx_buf[] = TPM_COMMAND_BUFFER;
	/*  Response buffer  */
	uint8_t rx_buf[8];

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
		LOG_INF("SPI device not found");
		return 0;
	}

	if (!device_is_ready(spi_dev)) {
		LOG_INF("%s: device not ready.", spi_dev->name);
		return 0;
	}

	LOG_INF("Starting TPM probe...");
	LOG_INF("SPI device: %s", spi_dev->name);
	LOG_INF("SPI configuration:");
	LOG_INF("  - Frequency: %d Hz", spi_cfg.frequency);
	LOG_INF("  - Mode: %s",
		(spi_cfg.operation & SPI_MODE_CPOL)
			? ((spi_cfg.operation & SPI_MODE_CPHA) ? "MODE3" : "MODE2")
			: ((spi_cfg.operation & SPI_MODE_CPHA) ? "MODE1" : "MODE0"));
	LOG_INF("  - Word size: %d bits", SPI_WORD_SIZE_GET(spi_cfg.operation));
	LOG_INF("  - Slave address: %d", spi_cfg.slave);

	/*  Perform SPI transaction  */
	int ret = spi_transceive(spi_dev, &spi_cfg, &tx, &rx);

	if (ret) {
		LOG_INF("SPI transaction failed: %d", ret);
		return 0;
	}

	/*  Print received data  */
	LOG_HEXDUMP_INF(rx_buf, sizeof(rx_buf), "Received data");

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
		LOG_INF("Success: Received response matches expected values");
	} else {
		LOG_INF("Warning: Response does not match expected values");
		LOG_INF("Expected: ");
		LOG_HEXDUMP_INF(expected, sizeof(expected), "Expected");
	}

	/*  Initialize TPM context  */
	_tpm_user_ctx.dev = spi_dev;
	_tpm_user_ctx.cfg = &spi_cfg;
	TPM2_CTX *ctx = &_tpm_ctx;

	int rc;

	LOG_INF("Initializing TPM...");
	rc = TPM2_Init(ctx, _tpm_io, &_tpm_user_ctx);
	if (rc != TPM_RC_SUCCESS) {
		LOG_INF("TPM_Init failed 0x%x", rc);
		return 0;
	}

	LOG_INF("TPM Startup...");
	Startup_In _startup = {
		.startupType = TPM_SU_CLEAR,
	};
	rc = TPM2_Startup(&_startup);
	if (rc != TPM_RC_SUCCESS) {
		LOG_INF("TPM_Startup failed 0x%x", rc);
		return 0;
	}

	LOG_INF("Performing TPM self test...");
	SelfTest_In _selfTest = {
		.fullTest = YES,
	};
	rc = TPM2_SelfTest(&_selfTest);
	if (rc != TPM_RC_SUCCESS) {
		LOG_INF("TPM_SelfTest failed 0x%x", rc);
		return 0;
	}
	LOG_INF("TPM probe completed.");

	return 0;
}

int _tpm_io(struct TPM2_CTX *ctx, const BYTE *tx, BYTE *rx, UINT16 size, void *userCtx)
{
	struct spi_buf tx_bufs[] = {
		{
			.buf = tx,
			.len = size,
		},
	};
	struct spi_buf rx_bufs[] = {
		{
			.buf = rx,
			.len = size,
		},
	};
	struct spi_buf_set _tx = {
		.buffers = tx_bufs,
		.count = ARRAY_SIZE(tx_bufs),
	};
	struct spi_buf_set _rx = {
		.buffers = rx_bufs,
		.count = ARRAY_SIZE(rx_bufs),
	};

	struct {
		const struct device *dev;
		struct spi_config *cfg;
	} *user_ctx = userCtx;

	int ret = spi_transceive(user_ctx->dev, user_ctx->cfg, &_tx, &_rx);

	LOG_HEXDUMP_DBG(tx, size, "TX");
	LOG_HEXDUMP_DBG(rx, size, "RX");

	k_msleep(10);

	if (ret) {
		LOG_ERR("SPI transaction failed: %d", ret);
		return TPM_RC_FAILURE;
	}

	return TPM_RC_SUCCESS;
}
