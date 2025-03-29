/**
 * Copyright 2024 (c) SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/drivers/clock_control.h>
#if defined(CONFIG_SDHC_DW_DMA)
#include <zephyr/drivers/dma.h>
#endif
#include <zephyr/cache.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/sd/sd_spec.h>
#include <zephyr/sd/sd.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/sem.h>
#include "sdhc_dw.h"

#ifndef CMD_SWITCH_FUNC
#define CMD_SWITCH_FUNC (6 << 4)
#endif

#define DT_DRV_COMPAT snps_dw_mshc

LOG_MODULE_REGISTER(sdhc_dw, CONFIG_SDHC_LOG_LEVEL);

struct sdhc_dw_data {
	DEVICE_MMIO_RAM;
	uint32_t prev_opcode;
	uint32_t host_freq;

#if defined(CONFIG_SDHC_DW_DMA)
	/* NOTE: This buffer must be mapped directly
	 * since virt/phys address is the same
	 */
	uint32_t *dma_buf;
	uint32_t dma_buf_len;

	struct dma_config dma_rx_cfg;
	struct dma_config dma_tx_cfg;
	struct dma_block_config dma_blk_cfg;
	struct k_sem dma_sync;
	struct k_spinlock dma_lock;
#endif
};

struct sdhc_dw_config {
	union {
		DEVICE_MMIO_ROM;
		uint32_t port;
	};
	const struct device *clk_dev;
#if defined(CONFIG_SDHC_DW_DMA)
	const struct device *dma_dev;
	uint32_t dma_channel;
#endif
	clock_control_subsys_t clk_subsys;
	uint32_t data_addr;
	uint32_t fifo_depth;
	uint32_t dma_burst;
	int non_removable;
	struct gpio_dt_spec cd_gpio;
};

static uint32_t _res_opcode_convert(bool is_spi_mode, uint32_t z_opcode, struct sdhc_dw_data *data)
{
	/* NOTE: CMD_SWITCH and ACMD_SWITCH_BUS_WIDTH are the same, need to determine which is */
	if (data->prev_opcode != SD_APP_CMD && is_spi_mode && z_opcode == SD_SWITCH) {
		z_opcode = CMD_SWITCH_FUNC;
	}
	data->prev_opcode = z_opcode;
	return z_opcode;
}

static uint32_t _dw_cmd_prepare(const struct sdhc_command *_cmd, struct sdhc_dw_data *dev_data)
{
	bool is_spi_cmd = _cmd->response_type & SDHC_SPI_RESPONSE_TYPE_MASK;
	uint32_t cmd = _res_opcode_convert(is_spi_cmd, _cmd->opcode, dev_data);
	uint32_t cmd_data = 0;

	switch (cmd) {
	/* No response commands */
	case SD_GO_IDLE_STATE:
		cmd_data = SDMMC_CMD_INIT;
		break;
	/* case CMD_SET_DSR: (4) */
	case SD_GO_INACTIVE_STATE:
		cmd_data = cmd;
		break;

	/* Long response commands */
	case SD_ALL_SEND_CID:
	case SD_SEND_CSD:
	case SD_SEND_CID:
		cmd_data = (SDMMC_CMD_RESP_EXP + SDMMC_CMD_RESP_LONG + cmd);
		break;

	/* Short response commands */
	/* CMD_GO_IRQ_STATE TBD */
	case MMC_SEND_OP_COND:
	case SD_SEND_RELATIVE_ADDR:
	/* TBD: MMC_SEND_RELATIVE_ADDR: */
	case SD_SET_BLOCK_SIZE:
	case SD_SET_BLOCK_COUNT:
	/* CMD_SET_WRITE_PROT TBD */
	/* CMD_CLR_WRITE_PROT TBD */
	case SD_ERASE_BLOCK_START:
	case SD_ERASE_BLOCK_END:
	/* CMD_GEN_CMD TBD */
	case SD_APP_SEND_OP_COND:
	case SD_APP_SET_BUS_WIDTH:
	case SD_SELECT_CARD:
	case SD_SEND_STATUS:
	case SD_APP_CMD:
		cmd_data = (SDMMC_CMD_RESP_EXP + cmd);
		break;
	case MMC_SEND_EXT_CSD:
		if (is_spi_cmd) {
			cmd_data = (SDMMC_CMD_RESP_EXP + cmd);
		} else {
			cmd_data = (SDMMC_CMD_PRV_DAT_WAIT + SDMMC_CMD_DAT_EXP +
				    SDMMC_CMD_RESP_CRC + SDMMC_CMD_RESP_EXP + cmd);
		}
		break;

	/* Stop&abort command */
	case SD_STOP_TRANSMISSION:
		cmd_data = (SDMMC_CMD_RESP_EXP + SDMMC_CMD_STOP + cmd);
		break;

	/* Have data command write */
	case SD_WRITE_SINGLE_BLOCK:
		cmd_data = (SDMMC_CMD_PRV_DAT_WAIT + SDMMC_CMD_DAT_WR + SDMMC_CMD_DAT_EXP +
			    SDMMC_CMD_RESP_CRC + SDMMC_CMD_RESP_EXP + cmd) &
			   ~SDMMC_CMD_SEND_STOP;
		break;
	case SD_WRITE_MULTIPLE_BLOCK:
		if (is_spi_cmd) {
			cmd_data =
				(SDMMC_CMD_SEND_STOP + SDMMC_CMD_PRV_DAT_WAIT + SDMMC_CMD_DAT_WR +
				 SDMMC_CMD_DAT_EXP + SDMMC_CMD_RESP_CRC + SDMMC_CMD_RESP_EXP + cmd);
		} else {
			cmd_data = (SDMMC_CMD_PRV_DAT_WAIT + SDMMC_CMD_DAT_WR + SDMMC_CMD_DAT_EXP +
				    SDMMC_CMD_RESP_CRC + SDMMC_CMD_RESP_EXP + cmd) &
				   ~SDMMC_CMD_SEND_STOP;
		}
		break;
	/* Have data command read */
	case SD_READ_MULTIPLE_BLOCK:
		if (is_spi_cmd) {
			cmd_data =
				(SDMMC_CMD_SEND_STOP + SDMMC_CMD_PRV_DAT_WAIT + SDMMC_CMD_DAT_EXP +
				 SDMMC_CMD_RESP_CRC + SDMMC_CMD_RESP_EXP + cmd);
		} else {
			cmd_data = (SDMMC_CMD_PRV_DAT_WAIT + SDMMC_CMD_DAT_EXP +
				    SDMMC_CMD_RESP_CRC + SDMMC_CMD_RESP_EXP + cmd) &
				   ~SDMMC_CMD_SEND_STOP;
		}
		break;
	case SD_APP_SEND_SCR:
	case SD_READ_SINGLE_BLOCK:
		cmd_data = (SDMMC_CMD_PRV_DAT_WAIT + SDMMC_CMD_DAT_EXP + SDMMC_CMD_RESP_CRC +
			    SDMMC_CMD_RESP_EXP + cmd);
		break;
	case CMD_SWITCH_FUNC:
		/* TBD: MMC_SEND_EXT_CSD: (8 << 4) */
		cmd_data = (SDMMC_CMD_PRV_DAT_WAIT + SDMMC_CMD_DAT_EXP + SDMMC_CMD_RESP_CRC +
			    SDMMC_CMD_RESP_EXP + (cmd >> 4));
		break;
	default:
		LOG_WRN("unknown command %d.", cmd);
		return -EINVAL;
	}
	cmd_data |= SDMMC_CMD_START;
	return cmd_data;
}

static bool _is_long_response(uint32_t z_resp_type)
{
	bool res;

	z_resp_type &= 0x0F;

	switch (z_resp_type) {
	default:
		LOG_WRN("Unsupported response type: %X", z_resp_type);
		/* fall through */
	case SD_RSP_TYPE_NONE:
	case SD_RSP_TYPE_R1:
	case SD_RSP_TYPE_R1b:
	case SD_RSP_TYPE_R3:
	case SD_RSP_TYPE_R4:
	case SD_RSP_TYPE_R5:
	case SD_RSP_TYPE_R6:
	case SD_RSP_TYPE_R7:
		res = false;
		break;
	case SD_RSP_TYPE_R2:
		res = true;
		break;
	}
	return res;
}

static int sdhc_dw_set_fifo_threshold(const struct device *dev, uint32_t threshold, uint32_t burst)
{
	dw_writel(dev, SDMMC_FIFOTH, SDMMC_SET_FIFOTH(burst, threshold, threshold));
	return 0;
}

static int sdhc_dw_set_power(const struct device *dev, enum sdhc_power power)
{
	int val;

	switch (power) {
	case SDHC_POWER_OFF:
		val = 0;
		break;
	case SDHC_POWER_ON:
		val = 0xFF;
		break;
	default:
		LOG_WRN("Unsupported power mode: %d", power);
		return -EINVAL;
	}
	dw_writel(dev, SDMMC_PWREN, val);
	return 0;
}

static int sdhc_dw_set_clock(const struct device *dev, uint32_t clk)
{
	int div_ = 0;
	const struct sdhc_dw_data *data = dev->data;
	const uint32_t freq = data->host_freq;
	uint32_t temp;

	for (int i = 1; i < 256; i++) {
		if (freq / (2 * i) <= clk) {
			div_ = i;
			break;
		}
	}

	/* disable clock */
	dw_writel(dev, SDMMC_CLKENA, 0);
	dw_writel(dev, SDMMC_CMD, SDMMC_CMD_START | SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT);
	if (dw_readl_poll(dev, SDMMC_CMD, temp, (temp & SDMMC_CMD_START) == 0, 10, 100)) {
		return -EIO;
	}

	/* set clock div */
	dw_writel(dev, SDMMC_CLKSRC, 0);
	dw_writel(dev, SDMMC_CLKDIV, div_);
	dw_writel(dev, SDMMC_CMD, SDMMC_CMD_START | SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT);
	if (dw_readl_poll(dev, SDMMC_CMD, temp, (temp & SDMMC_CMD_START) == 0, 10, 100)) {
		return -EIO;
	}

	/* enable clock */
	dw_writel(dev, SDMMC_CLKENA, SDMMC_CLKEN_ENABLE | SDMMC_CLKEN_LOW_PWR);
	dw_writel(dev, SDMMC_CMD, SDMMC_CMD_START | SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT);
	if (dw_readl_poll(dev, SDMMC_CMD, temp, (temp & SDMMC_CMD_START) == 0, 10, 100)) {
		return -EIO;
	}

	return 0;
}

static int sdhc_dw_set_bus_width(const struct device *dev, enum sdhc_bus_width width)
{
	uint32_t reg;
	uint32_t val;

	reg = SDMMC_CTYPE;
	if (width == SDHC_BUS_WIDTH1BIT) {
		val = SDMMC_CTYPE_1BIT;
	} else if (width == SDHC_BUS_WIDTH4BIT) {
		val = SDMMC_CTYPE_4BIT;
	} else {
		/* SDMMC_CTYPE_8BIT is not supported */
		LOG_WRN("Unsupported bus width: %d", width);
		return -EINVAL;
	}

	dw_writel(dev, reg, val);
	return 0;
}

static int sdhc_dw_read_poll(const struct device *dev, uint32_t *addr, uint32_t len)
{
	const struct sdhc_dw_config *config = dev->config;
	const uint32_t data_offset = config->data_addr;
	int timeout = 0x10000;
	uint32_t reg_status;
	uint32_t fifo_cnt;
	uint32_t iter;

	if ((unsigned int)addr & 3 || len & 3) {
		LOG_WRN("Unaligned address or length");
		return -EINVAL;
	}
	iter = len / 4;

	while (iter) {
		reg_status = dw_readl(dev, SDMMC_STATUS);

		if (!(reg_status & SDMMC_STATUS_FIFO_EMPTY)) {
			fifo_cnt = SDMMC_GET_FCNT(reg_status);
			if (unlikely(fifo_cnt > iter)) {
				fifo_cnt = iter;
			}
			for (int i = 0; i < fifo_cnt; i++, addr++) {
				*addr = dw_readl(dev, data_offset);
			}
			iter -= fifo_cnt;
			continue;
		} else if (timeout-- <= 0) {
			LOG_WRN_ONCE("Timeout");
			return -ETIMEDOUT;
		}
		k_busy_wait(10);
	}

	return dw_readl_poll(dev, SDMMC_RINTSTS, reg_status, (reg_status & SDMMC_INT_DATA_OVER), 10,
			     10000);
}

static int sdhc_dw_write_poll(const struct device *dev, const uint32_t *addr, uint32_t len)
{

	const struct sdhc_dw_config *config = dev->config;
	const uint32_t data_offset = config->data_addr;
	int timeout = 0x10000;
	uint32_t reg_status;
	uint32_t fifo_cnt;
	uint32_t fifo_threshold;
	uint32_t iter;

	if ((unsigned int)addr & 3 || len & 3) {
		LOG_WRN("Unaligned address or length");
		return -EINVAL;
	}

	iter = len / 4;

	fifo_threshold = SDMMC_GET_TX_WMARK(dw_readl(dev, SDMMC_FIFOTH));

	while (iter) {
		reg_status = dw_readl(dev, SDMMC_STATUS);

		if (!(reg_status & SDMMC_STATUS_FIFO_FULL) &&
		    fifo_threshold > SDMMC_GET_FCNT(reg_status)) {
			fifo_cnt = fifo_threshold - SDMMC_GET_FCNT(reg_status);
			if (unlikely(fifo_cnt > iter)) {
				fifo_cnt = iter;
			}
			for (int i = 0; i < fifo_cnt; i++, addr++) {
				dw_writel(dev, data_offset, *addr);
			}
			iter -= fifo_cnt;
			continue;
		} else if (timeout-- <= 0) {
			LOG_WRN_ONCE("Timeout");
			return -ETIMEDOUT;
		}

		k_busy_wait(10);
	}

	return dw_readl_poll(dev, SDMMC_RINTSTS, reg_status, (reg_status & SDMMC_INT_DATA_OVER), 10,
			     10000);
}

#if defined(CONFIG_SDHC_DW_DMA)

static inline bool _dw_using_dma(const struct device *dev)
{
	const struct sdhc_dw_config *config = dev->config;

	return config->dma_dev != NULL;
}

static void sdhc_dma_callback(const struct device *dev, void *user_data, uint32_t channel,
			      int status)
{
	struct device *sdhc_dev = (struct device *)user_data;
	struct sdhc_dw_data *data = sdhc_dev->data;

	k_sem_give(&data->dma_sync);

	LOG_DBG("DMA transfer complete");
}

static int sdhc_dw_write_dma(const struct device *dev, const uint32_t *addr, uint32_t len)
{
	const struct sdhc_dw_config *config = dev->config;
	struct sdhc_dw_data *data = dev->data;
	uint32_t peripheral_addr = DEVICE_MMIO_ROM_PTR(dev)->phys_addr + config->data_addr;
	uint32_t memory_addr = (uint32_t)data->dma_buf;
	struct dma_config *dma_cfg = &data->dma_tx_cfg;
	struct dma_block_config *blk_cfg = &data->dma_blk_cfg;
	uint32_t reg_status;
	int rc = 0;

	if ((unsigned int)addr & 3 || len & 3) {
		LOG_WRN("Unaligned address or length");
		return -EINVAL;
	}

	if (len > data->dma_buf_len) {
		LOG_WRN("Data length exceeds DMA buffer size");
		return -EINVAL;
	}

	K_SPINLOCK(&data->dma_lock) {
		/* load data into dma buffer */
		memcpy(data->dma_buf, addr, len);

		/* prepare dma transfer */
		dma_cfg->channel_direction = MEMORY_TO_PERIPHERAL;
		dma_cfg->complete_callback_en = 1;
		dma_cfg->error_callback_dis = 0;
		dma_cfg->source_handshake = 0;
		dma_cfg->dest_handshake = 0;
		dma_cfg->cyclic = 0;
		dma_cfg->source_data_size = 4;
		dma_cfg->dest_data_size = 4;
		dma_cfg->source_burst_length = 4;
		dma_cfg->dest_burst_length = 4;
		dma_cfg->block_count = 1;
		dma_cfg->head_block = blk_cfg;
		dma_cfg->user_data = (void *)dev;
		dma_cfg->dma_callback = sdhc_dma_callback;

		blk_cfg->source_address = memory_addr;
		blk_cfg->dest_address = peripheral_addr;
		blk_cfg->block_size = len / 4;

		rc = dma_config(config->dma_dev, config->dma_channel, dma_cfg);
		if (rc) {
			LOG_ERR("Failed to configure DMA channel");
			K_SPINLOCK_BREAK;
		}

		/* flush dcache before dma transfer */
		sys_cache_data_flush_range(data->dma_buf, len);

		/* dma transfer */
		LOG_DBG("Starting DMA TX transfer");
		rc = dma_start(config->dma_dev, config->dma_channel);
		if (rc) {
			LOG_ERR("Failed to start DMA transfer");
			K_SPINLOCK_BREAK;
		}

		/* start dma transfer */
		rc = k_sem_take(&data->dma_sync, K_MSEC(1000));
		if (rc) {
			LOG_ERR("DMA transfer timeout");
			K_SPINLOCK_BREAK;
		}
	}

	dma_stop(config->dma_dev, config->dma_channel);
	if (rc) {
		return rc;
	}

	return dw_readl_poll(dev, SDMMC_RINTSTS, reg_status, (reg_status & SDMMC_INT_DATA_OVER), 10,
			     10000);
}

static int sdhc_dw_read_dma(const struct device *dev, uint32_t *addr, uint32_t len)
{
	const struct sdhc_dw_config *config = dev->config;
	struct sdhc_dw_data *data = dev->data;
	uint32_t peripheral_addr = DEVICE_MMIO_ROM_PTR(dev)->phys_addr + config->data_addr;
	uint32_t memory_addr = (uint32_t)data->dma_buf;
	struct dma_config *dma_cfg = &data->dma_rx_cfg;
	struct dma_block_config *blk_cfg = &data->dma_blk_cfg;
	uint32_t reg_status;
	int rc = 0;

	if ((unsigned int)addr & 3 || len & 3) {
		LOG_WRN("Unaligned address or length");
		return -EINVAL;
	}

	if (len > data->dma_buf_len) {
		LOG_WRN("Data length exceeds DMA buffer size");
		return -EINVAL;
	}

	K_SPINLOCK(&data->dma_lock) {

		/* prepare dma transfer */
		dma_cfg->channel_direction = PERIPHERAL_TO_MEMORY;
		dma_cfg->complete_callback_en = 1;
		dma_cfg->error_callback_dis = 0;
		dma_cfg->source_handshake = 0;
		dma_cfg->dest_handshake = 0;
		dma_cfg->cyclic = 0;
		dma_cfg->source_data_size = 4;
		dma_cfg->dest_data_size = 4;
		dma_cfg->source_burst_length = 4;
		dma_cfg->dest_burst_length = 4;
		dma_cfg->block_count = 1;
		dma_cfg->head_block = blk_cfg;
		dma_cfg->user_data = (void *)dev;
		dma_cfg->dma_callback = sdhc_dma_callback;

		blk_cfg->source_address = peripheral_addr;
		blk_cfg->dest_address = memory_addr;
		blk_cfg->block_size = len / 4;

		rc = dma_config(config->dma_dev, config->dma_channel, dma_cfg);
		if (rc) {
			LOG_ERR("Failed to configure DMA channel");
			K_SPINLOCK_BREAK;
		}

		/* dma transfer */
		LOG_DBG("Starting DMA RX transfer");
		rc = dma_start(config->dma_dev, config->dma_channel);
		if (rc) {
			LOG_ERR("Failed to start DMA transfer");
			K_SPINLOCK_BREAK;
		}

		/* start dma transfer */
		rc = k_sem_take(&data->dma_sync, K_MSEC(1000));
		if (rc) {
			LOG_ERR("DMA transfer timeout");
			K_SPINLOCK_BREAK;
		}

		/* invalidate dcache after dma transfer */
		sys_cache_data_invd_range(data->dma_buf, len);

		/* copy data from dma buffer */
		memcpy(addr, data->dma_buf, len);
	}
	dma_stop(config->dma_dev, config->dma_channel);

	return dw_readl_poll(dev, SDMMC_RINTSTS, reg_status, (reg_status & SDMMC_INT_DATA_OVER), 10,
			     10000);
}
#endif /* CONFIG_SDHC_DW_DMA */

static int sdhc_dw_write(const struct device *dev, const uint32_t *addr, uint32_t len)
{
#if defined(CONFIG_SDHC_DW_DMA)
	if (_dw_using_dma(dev)) {
		return sdhc_dw_write_dma(dev, addr, len);
	}
#endif
	return sdhc_dw_write_poll(dev, addr, len);
}

static int sdhc_dw_read(const struct device *dev, uint32_t *addr, uint32_t len)
{
#if defined(CONFIG_SDHC_DW_DMA)
	if (_dw_using_dma(dev)) {
		return sdhc_dw_read_dma(dev, addr, len);
	}
#endif
	return sdhc_dw_read_poll(dev, addr, len);
}

static int sdhc_dw_request(const struct device *dev, struct sdhc_command *cmd,
			   struct sdhc_data *data)
{
	const uint32_t per_loop_delay_us = 1;
	uint32_t rcmd;
	uint32_t temp;
	int timeout = (cmd->retries + 1) * cmd->timeout_ms * 1000 / per_loop_delay_us;
	enum {
		rd,
		wr,
		none
	} dir = none;

	rcmd = _dw_cmd_prepare(cmd, dev->data);

	if (rcmd & SDMMC_CMD_DAT_WR) {
		dir = wr;
	} else if (rcmd & SDMMC_CMD_DAT_EXP) {
		dir = rd;
	} else {
		dir = none;
	}

	/* clear status */
	dw_writel(dev, SDMMC_RINTSTS, 0xFFFFFFFF);

	/* set data transfer if needed */
	if (dir != none) {
		dw_writel(dev, SDMMC_BYTCNT, data->blocks * data->block_size);
		dw_writel(dev, SDMMC_BLKSIZ, data->block_size);
	}

	/* send command */
	dw_writel(dev, SDMMC_CMDARG, cmd->arg);
	dw_writel(dev, SDMMC_CMD, rcmd);

	/* wait for command to complete */
	temp = dw_readl(dev, SDMMC_RINTSTS);
	while (!(temp & SDMMC_INT_CMD_DONE) && timeout--) {
		k_busy_wait(per_loop_delay_us);
		temp = dw_readl(dev, SDMMC_RINTSTS);
	}

	if (timeout < 0 || (temp & SDMMC_INT_RTO) || (temp & SDMMC_INT_DRTO)) {
		LOG_WRN_ONCE("Command(%x) timeout", cmd->opcode & 0xFF);
		return -ETIMEDOUT;
	} else if (rcmd & SDMMC_CMD_RESP_CRC && temp & (SDMMC_INT_RCRC | SDMMC_INT_DCRC)) {
		LOG_WRN_ONCE("Response CRC error");
		return -EIO;
	}

	LOG_DBG("Command(%08x), status(%08x), timeout(%08x), X(%c)", cmd->opcode, temp, timeout,
		dir == rd   ? 'R'
		: dir == wr ? 'W'
			    : 'N');

	dw_writel(dev, SDMMC_RINTSTS, SDMMC_INT_CMD_DONE);

	if (_is_long_response(cmd->response_type)) {
		cmd->response[0] = dw_readl(dev, SDMMC_RESP0);
		cmd->response[1] = dw_readl(dev, SDMMC_RESP1);
		cmd->response[2] = dw_readl(dev, SDMMC_RESP2);
		cmd->response[3] = dw_readl(dev, SDMMC_RESP3);
	} else {
		cmd->response[0] = dw_readl(dev, SDMMC_RESP0);
	}

	if (dir != none) {
		if (dir == rd) {
			if (sdhc_dw_read(dev, data->data, data->blocks * data->block_size)) {
				LOG_WRN_ONCE("Read data timeout");
				return -ETIMEDOUT;
			}
		} else {
			if (sdhc_dw_write(dev, data->data, data->blocks * data->block_size)) {
				LOG_WRN_ONCE("Write data timeout");
				return -ETIMEDOUT;
			}
		}
		data->bytes_xfered = data->blocks * data->block_size;
	}

	return 0;
}

static int sdhc_dw_set_io(const struct device *dev, struct sdhc_io *io)
{
	int ret;
	const struct sdhc_dw_config *config = dev->config;

	/* Deal with power mode */
	ret = sdhc_dw_set_power(dev, io->power_mode);
	if (ret) {
		LOG_WRN("Failed to set power mode");
		return -EIO;
	}

	/* Deal with bus clock */
	ret = sdhc_dw_set_clock(dev, (unsigned int)(io->clock));
	if (ret) {
		LOG_WRN("Failed to set clock rate");
		return -EIO;
	}

	/* Deal with bus width */
	ret = sdhc_dw_set_bus_width(dev, io->bus_width);
	if (ret) {
		LOG_WRN("Failed to set bus width");
		return -EIO;
	}

	sdhc_dw_set_fifo_threshold(dev, config->fifo_depth, config->dma_burst);

#if defined(CONFIG_SDHC_DW_DMA)
	if (_dw_using_dma(dev)) {
		uint32_t temp;

		temp = dw_readl(dev, SDMMC_CTRL) | SDMMC_CTRL_DMA_ENABLE;
		dw_writel(dev, SDMMC_CTRL, temp);
	}
#endif

	return 0;
}

static int sdhc_dw_get_host_props(const struct device *dev, struct sdhc_host_props *props)
{
	memset(props, 0, sizeof(struct sdhc_host_props));
	props->f_max = 100000000;
	props->f_min = 400000;
	props->power_delay = 100;
	props->host_caps.bus_4_bit_support = 1;
	props->host_caps.high_spd_support = 1;
	props->host_caps.vol_330_support = 1;
	props->host_caps.vol_300_support = 1;
	props->host_caps.vol_180_support = 0;
	props->host_caps.hs200_support = 1;
	props->host_caps.hs400_support = 1;
	props->max_current_330 = 200;
	props->max_current_300 = 200;
	props->max_current_180 = 200;
	props->is_spi = false;

	return 0;
}

static int sdhc_dw_card_present(const struct device *dev)
{
	const struct sdhc_dw_config *config = dev->config;

	if (config->non_removable) {
		return 1;
	}
	return gpio_pin_get(config->cd_gpio.port, config->cd_gpio.pin);
}

static int sdhc_dw_reset(const struct device *dev)
{
	uint32_t temp;

	temp = dw_readl(dev, SDMMC_CTRL) | SDMMC_CTRL_ALL_RESET_FLAGS;
	dw_writel(dev, SDMMC_CTRL, temp);

	return dw_readl_poll(dev, SDMMC_CTRL, temp, (temp & SDMMC_CTRL_ALL_RESET_FLAGS) == 0, 10,
			     1000);
}

static int sdhc_dw_card_busy(const struct device *dev)
{
	uint32_t status;

	status = dw_readl(dev, SDMMC_STATUS);
	/* FSM-Idle = bit[7:4] = 0 */
	return (status & 0xF0) ? 1 : 0;
}

static const struct sdhc_driver_api sdhc_dw_api = {
	.request = sdhc_dw_request,
	.set_io = sdhc_dw_set_io,
	.get_host_props = sdhc_dw_get_host_props,
	.get_card_present = sdhc_dw_card_present,
	.reset = sdhc_dw_reset,
	.card_busy = sdhc_dw_card_busy,
};

static int sdhc_dw_init(const struct device *dev)
{
	const struct sdhc_dw_config *config = dev->config;
	struct sdhc_dw_data *data = dev->data;
	int ret;
	uint32_t rate;

	DEVICE_MMIO_MAP(dev, K_MEM_CACHE_NONE);

	ret = clock_control_get_rate(config->clk_dev, config->clk_subsys, &rate);
	if (ret) {
		LOG_ERR("Failed to get clock rate");
		return ret;
	}
	data->host_freq = rate;

	if (!config->non_removable && device_is_ready(config->cd_gpio.port)) {
		ret = gpio_pin_configure_dt(&config->cd_gpio, GPIO_INPUT);
		if (ret) {
			LOG_ERR("Failed to configure CD GPIO");
			return ret;
		}
	} else if (!config->non_removable) {
		LOG_ERR("CD GPIO not available");
		return -EINVAL;
	}

#if defined(CONFIG_SDHC_DW_DMA)
	k_sem_init(&data->dma_sync, 0, 1);
#endif

	return 0;
}

#define SDHC_DW_INIT(n)                                                                            \
	IF_ENABLED(CONFIG_SDHC_DW_DMA, (static uint32_t __aligned(64) _dw_dma_buf_##n[0x2000];))   \
	static const struct sdhc_dw_config sdhc_dw_config_##n = {                                  \
		COND_CODE_1(DT_INST_PROP_OR(n, io_mapped, 0), (.port = DT_INST_REG_ADDR(n), ),     \
			    (DEVICE_MMIO_ROM_INIT(DT_DRV_INST(n)), ))                              \
			.clk_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),                          \
		IF_ENABLED(                                                                        \
			CONFIG_SDHC_DW_DMA,                                                        \
			(.dma_dev = COND_CODE_1(                                                   \
				 DT_INST_NODE_HAS_PROP(n, dmas),                                   \
				 (DEVICE_DT_GET_OR_NULL(DT_INST_DMAS_CTLR_BY_IDX(n, 0))), (NULL)), \
			 .dma_channel =                                                            \
				 COND_CODE_1(DT_INST_NODE_HAS_PROP(n, dmas),                       \
					     (DT_INST_DMAS_CELL_BY_IDX(n, 0, channel)), (0)), ))   \
			.clk_subsys = (clock_control_subsys_t)DT_INST_PHA(n, clocks, clkid),       \
		.data_addr = DT_INST_PROP(n, data_addr),                                           \
		.fifo_depth = DT_INST_PROP(n, fifo_depth),                                         \
		.dma_burst = DT_INST_PROP(n, dma_burst),                                           \
		.non_removable = DT_INST_PROP(n, non_removable),                                   \
		.cd_gpio = GPIO_DT_SPEC_GET_BY_IDX_OR(DT_DRV_INST(n), cd_gpios, 0, {0}),           \
	};                                                                                         \
	static struct sdhc_dw_data sdhc_dw_data_##n = {IF_ENABLED(                                 \
		CONFIG_SDHC_DW_DMA,                                                                \
		(.dma_buf = COND_CODE_1(CONFIG_SDHC_DW_DMA, (&_dw_dma_buf_##n[0]), (NULL)),        \
		 .dma_buf_len =                                                                    \
			 COND_CODE_1(CONFIG_SDHC_DW_DMA, (sizeof(_dw_dma_buf_##n)), (0)), ))};     \
	DEVICE_DT_INST_DEFINE(n, sdhc_dw_init, NULL, &sdhc_dw_data_##n, &sdhc_dw_config_##n,       \
			      POST_KERNEL, CONFIG_SDHC_INIT_PRIORITY, &sdhc_dw_api);

DT_INST_FOREACH_STATUS_OKAY(SDHC_DW_INIT)
