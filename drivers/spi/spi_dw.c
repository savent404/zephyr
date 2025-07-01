/*
 * Copyright (c) 2015 Intel Corporation.
 * Copyright (c) 2023 Synopsys, Inc. All rights reserved.
 * Copyright (c) 2023 Meta Platforms
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT snps_designware_spi

/* spi_dw.c - Designware SPI driver implementation */

#define LOG_LEVEL CONFIG_SPI_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(spi_dw);

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/arch/cpu.h>

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/pm/device.h>

#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#ifdef CONFIG_IOAPIC
#include <zephyr/drivers/interrupt_controller/ioapic.h>
#endif

#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/spi/rtio.h>
#include <zephyr/irq.h>
#include <zephyr/sys/device_mmio.h>

#ifdef CONFIG_DCACHE
#include <zephyr/cache.h>
#endif

#ifdef CONFIG_MMU
#include <zephyr/kernel/mm.h>
#endif

#include "spi_dw.h"
#include "spi_context.h"

#ifdef CONFIG_PINCTRL
#include <zephyr/drivers/pinctrl.h>
#endif

#ifdef CONFIG_SPI_DW_DMA
#include <zephyr/drivers/dma.h>

/* DMA min transfer threshold, less than this threshold use interrupt mode */
#define SPI_DW_DMA_THRESHOLD 32

static void spi_dw_dma_callback(const struct device *dma_dev, void *user_data, uint32_t channel,
				int status);

static inline bool has_tx_data(const struct spi_buf_set *tx_bufs)
{
	return tx_bufs && tx_bufs->count > 0;
}

static inline bool has_rx_data(const struct spi_buf_set *rx_bufs)
{
	return rx_bufs && rx_bufs->count > 0;
}

static inline void spi_dw_stop_dma_channels(const struct device *dev,
					    const struct spi_buf_set *tx_bufs,
					    const struct spi_buf_set *rx_bufs)
{
	const struct spi_dw_config *cfg = dev->config;

	if (has_tx_data(tx_bufs)) {
		dma_stop(cfg->dma_tx.dma_dev, cfg->dma_tx.channel);
	}
	if (has_rx_data(rx_bufs)) {
		dma_stop(cfg->dma_rx.dma_dev, cfg->dma_rx.channel);
	}
}
#endif

static inline bool spi_dw_is_slave(struct spi_dw_data *spi)
{
	return (IS_ENABLED(CONFIG_SPI_SLAVE) && spi_context_is_slave(&spi->ctx));
}

#ifdef CONFIG_SPI_DW_DMA
static void spi_dw_dma_callback(const struct device *dma_dev, void *user_data, uint32_t channel,
				int status)
{
	const struct device *spi_dev = (const struct device *)user_data;
	const struct spi_dw_config *cfg = spi_dev->config;
	struct spi_dw_data *spi = spi_dev->data;

	LOG_DBG("DMA callback from %s: channel %d, status %d", dma_dev->name, channel, status);

	if (status != DMA_STATUS_COMPLETE) {
		LOG_ERR("DMA transfer failed with status %d", status);
		spi->dma_stat |= SPI_DW_DMA_ERROR_FLAG;
	} else {
		if (channel == cfg->dma_tx.channel) {
			spi->dma_stat |= SPI_DW_DMA_TX_DONE_FLAG;
			LOG_DBG("DMA TX channel done");
		} else if (channel == cfg->dma_rx.channel) {
			spi->dma_stat |= SPI_DW_DMA_RX_DONE_FLAG;
			LOG_DBG("DMA RX channel done");
		}
	}

	/* If both TX and RX are done (or an error occurred), release the semaphore */
	if ((spi->dma_stat & SPI_DW_DMA_DONE_FLAG) == SPI_DW_DMA_TX_DONE_FLAG ||
	    (spi->dma_stat & SPI_DW_DMA_DONE_FLAG) == SPI_DW_DMA_RX_DONE_FLAG) {
		LOG_DBG("Both DMA channels done, giving semaphore.");
		k_sem_give(&spi->dma_sem);
	} else if ((spi->dma_stat & SPI_DW_DMA_ERROR_FLAG) == SPI_DW_DMA_ERROR_FLAG) {
		LOG_ERR("DMA error flag: %d", spi->dma_stat & SPI_DW_DMA_ERROR_FLAG);
		k_sem_give(&spi->dma_sem);
	}
}

static bool spi_dw_should_use_dma(const struct spi_dw_config *cfg,
				  const struct spi_buf_set *tx_bufs,
				  const struct spi_buf_set *rx_bufs)
{
	size_t total_len = 0;

	if (!cfg->dma_tx.dma_dev || !cfg->dma_rx.dma_dev) {
		return false;
	}

	if (tx_bufs) {
		for (size_t i = 0; i < tx_bufs->count; i++) {
			total_len += tx_bufs->buffers[i].len;
		}
	}

	if (rx_bufs) {
		for (size_t i = 0; i < rx_bufs->count; i++) {
			total_len += rx_bufs->buffers[i].len;
		}
	}

	return total_len >= SPI_DW_DMA_THRESHOLD;
}

static int spi_dw_configure_dma(const struct device *dev, const struct spi_buf_set *tx_bufs,
				const struct spi_buf_set *rx_bufs, uint8_t dfs)
{
	const struct spi_dw_config *cfg = dev->config;
	struct spi_dw_data *spi = dev->data;
	int ret = 0;
	uintptr_t spi_phys_addr;

	spi_phys_addr = DEVICE_MMIO_ROM_PTR(dev)->phys_addr;
	LOG_DBG("SPI physical address for DMA: 0x%lx", spi_phys_addr);

	/* configure TX DMA */
	if (has_tx_data(tx_bufs)) {
		/* Convert virtual address to physical address for DMA */
#ifdef CONFIG_MMU
		uintptr_t tx_phys_addr = k_mem_phys_addr(tx_bufs->buffers[0].buf);
#else
		uintptr_t tx_phys_addr = (uintptr_t)tx_bufs->buffers[0].buf;
#endif

		/* Flush cache before DMA operation */
#ifdef CONFIG_DCACHE
		sys_cache_data_flush_range(tx_bufs->buffers[0].buf, tx_bufs->buffers[0].len);
#endif

		/* use persistent dma_config */
		spi->tx_dma_cfg = cfg->dma_tx.dma_cfg;

		/* use persistent block config */
		spi->tx_blk_cfg = cfg->dma_tx.dma_blk_cfg;
		spi->tx_blk_cfg.source_address = (uint32_t)tx_phys_addr;
		spi->tx_blk_cfg.dest_address = spi_phys_addr + DW_SPI_REG_DR;
		spi->tx_blk_cfg.block_size = tx_bufs->buffers[0].len;
		spi->tx_blk_cfg.source_addr_adj = DMA_ADDR_ADJ_INCREMENT;
		spi->tx_blk_cfg.dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;

		spi->tx_dma_cfg.head_block = &spi->tx_blk_cfg;
		spi->tx_dma_cfg.user_data = (void *)dev;

		LOG_DBG("TX DMA config: src=0x%x (virt=0x%p), dest=0x%x, size=%d",
			spi->tx_blk_cfg.source_address, tx_bufs->buffers[0].buf,
			spi->tx_blk_cfg.dest_address, spi->tx_blk_cfg.block_size);

		ret = dma_config(cfg->dma_tx.dma_dev, cfg->dma_tx.channel, &spi->tx_dma_cfg);
		if (ret) {
			LOG_ERR("Failed to configure TX DMA: %d", ret);
			return ret;
		}
	}

	/* configure RX DMA */
	if (has_rx_data(rx_bufs)) {
		/* Convert virtual address to physical address for DMA */
#ifdef CONFIG_MMU
		uintptr_t rx_phys_addr = k_mem_phys_addr(rx_bufs->buffers[0].buf);
#else
		uintptr_t rx_phys_addr = (uintptr_t)rx_bufs->buffers[0].buf;
#endif

		/* Invalidate cache before DMA operation to ensure no stale data */
#ifdef CONFIG_DCACHE
		sys_cache_data_invd_range(rx_bufs->buffers[0].buf, rx_bufs->buffers[0].len);
#endif

		/* use persistent dma_config */
		spi->rx_dma_cfg = cfg->dma_rx.dma_cfg;

		/* use persistent block config */
		spi->rx_blk_cfg = cfg->dma_rx.dma_blk_cfg;
		spi->rx_blk_cfg.source_address = spi_phys_addr + DW_SPI_REG_DR;
		spi->rx_blk_cfg.dest_address = (uint32_t)rx_phys_addr;
		spi->rx_blk_cfg.block_size = rx_bufs->buffers[0].len;
		spi->rx_blk_cfg.source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
		spi->rx_blk_cfg.dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;

		spi->rx_dma_cfg.head_block = &spi->rx_blk_cfg;
		spi->rx_dma_cfg.user_data = (void *)dev;

		LOG_DBG("RX DMA config: src=0x%x, dest=0x%x (virt=0x%p), size=%d",
			spi->rx_blk_cfg.source_address, spi->rx_blk_cfg.dest_address,
			rx_bufs->buffers[0].buf, spi->rx_blk_cfg.block_size);

		ret = dma_config(cfg->dma_rx.dma_dev, cfg->dma_rx.channel, &spi->rx_dma_cfg);
		if (ret) {
			LOG_ERR("Failed to configure RX DMA: %d", ret);
			return ret;
		}
	}

	return 0;
}

static int spi_dw_start_dma(const struct device *dev, const struct spi_buf_set *tx_bufs,
			    const struct spi_buf_set *rx_bufs)
{
	const struct spi_dw_config *cfg = dev->config;
	struct spi_dw_data *spi = dev->data;
	int ret = 0;

	spi->dma_stat = 0;

	LOG_DBG("Starting DMA transfer - TX: %s, RX: %s",
		has_tx_data(tx_bufs) ? "enabled" : "disabled",
		has_rx_data(rx_bufs) ? "enabled" : "disabled");

	/* for full-duplex transfer, must start RX DMA first, then start TX DMA */
	if (has_rx_data(rx_bufs)) {
		LOG_DBG("Starting RX DMA: channel %d, slot %d", cfg->dma_rx.channel,
			cfg->dma_rx.dma_cfg.dma_slot);
		ret = dma_start(cfg->dma_rx.dma_dev, cfg->dma_rx.channel);
		if (ret) {
			LOG_ERR("Failed to start RX DMA: %d", ret);
			return ret;
		}
		LOG_DBG("RX DMA started successfully");
	} else {
		/* if no RX, mark RX as done */
		LOG_DBG("No RX buffers, marking RX as done");
		spi->dma_stat |= SPI_DW_DMA_RX_DONE_FLAG;
	}

	/* start TX DMA */
	if (has_tx_data(tx_bufs)) {
		LOG_DBG("Starting TX DMA: channel %d, slot %d", cfg->dma_tx.channel,
			cfg->dma_tx.dma_cfg.dma_slot);
		ret = dma_start(cfg->dma_tx.dma_dev, cfg->dma_tx.channel);
		if (ret) {
			LOG_ERR("Failed to start TX DMA: %d", ret);
			if (has_rx_data(rx_bufs)) {
				dma_stop(cfg->dma_rx.dma_dev, cfg->dma_rx.channel);
			}
			return ret;
		}
		LOG_DBG("TX DMA started successfully");
	} else {
		/* if no TX, mark TX as done */
		LOG_DBG("No TX buffers, marking TX as done");
		spi->dma_stat |= SPI_DW_DMA_TX_DONE_FLAG;
	}

	LOG_DBG("DMA start completed, status: 0x%02x", spi->dma_stat);
	return 0;
}
#endif /* CONFIG_SPI_DW_DMA */

static void completed(const struct device *dev, int error)
{
	struct spi_dw_data *spi = dev->data;
	struct spi_context *ctx = &spi->ctx;

	if (error) {
		goto out;
	}

	if (spi_context_tx_on(&spi->ctx) || spi_context_rx_on(&spi->ctx)) {
		return;
	}

out:
	/* need to give time for FIFOs to drain before issuing more commands */
	while (test_bit_sr_busy(dev)) {
	}

	/* Disabling interrupts */
	write_imr(dev, DW_SPI_IMR_MASK);
	/* Disabling the controller */
	clear_bit_ssienr(dev);

	if (!spi_dw_is_slave(spi)) {
		if (spi_cs_is_gpio(ctx->config)) {
			spi_context_cs_control(ctx, false);
		} else {
			write_ser(dev, 0);
		}
	}

	LOG_DBG("SPI transaction completed %s error", error ? "with" : "without");

	spi_context_complete(&spi->ctx, dev, error);
}

static void push_data(const struct device *dev)
{
	const struct spi_dw_config *info = dev->config;
	struct spi_dw_data *spi = dev->data;
	uint32_t data = 0U;
	uint32_t f_tx;

	if (spi_context_rx_on(&spi->ctx)) {
		f_tx = info->fifo_depth - read_txflr(dev) - read_rxflr(dev);
		if ((int)f_tx < 0) {
			f_tx = 0U; /* if rx-fifo is full, hold off tx */
		}
	} else {
		f_tx = info->fifo_depth - read_txflr(dev);
	}

	while (f_tx) {
		if (spi_context_tx_buf_on(&spi->ctx)) {
			switch (spi->dfs) {
			case 1:
				data = UNALIGNED_GET((uint8_t *)(spi->ctx.tx_buf));
				break;
			case 2:
				data = UNALIGNED_GET((uint16_t *)(spi->ctx.tx_buf));
				break;
			case 4:
				data = UNALIGNED_GET((uint32_t *)(spi->ctx.tx_buf));
				break;
			}
		} else if (spi_context_rx_on(&spi->ctx)) {
			/* No need to push more than necessary */
			if ((int)(spi->ctx.rx_len - spi->fifo_diff) <= 0) {
				break;
			}

			data = 0U;
		} else if (spi_context_tx_on(&spi->ctx)) {
			data = 0U;
		} else {
			/* Nothing to push anymore */
			break;
		}

		write_dr(dev, data);

		spi_context_update_tx(&spi->ctx, spi->dfs, 1);
		spi->fifo_diff++;

		f_tx--;
	}

	if (!spi_context_tx_on(&spi->ctx)) {
		/* prevents any further interrupts demanding TX fifo fill */
		write_txftlr(dev, 0);
	}
}

static void pull_data(const struct device *dev)
{
	const struct spi_dw_config *info = dev->config;
	struct spi_dw_data *spi = dev->data;

	while (read_rxflr(dev)) {
		uint32_t data = read_dr(dev);

		if (spi_context_rx_buf_on(&spi->ctx)) {
			switch (spi->dfs) {
			case 1:
				UNALIGNED_PUT(data, (uint8_t *)spi->ctx.rx_buf);
				break;
			case 2:
				UNALIGNED_PUT(data, (uint16_t *)spi->ctx.rx_buf);
				break;
			case 4:
				UNALIGNED_PUT(data, (uint32_t *)spi->ctx.rx_buf);
				break;
			}
		}

		spi_context_update_rx(&spi->ctx, spi->dfs, 1);
		spi->fifo_diff--;
	}

	if (!spi->ctx.rx_len && spi->ctx.tx_len < info->fifo_depth) {
		write_rxftlr(dev, spi->ctx.tx_len - 1);
	} else if (read_rxftlr(dev) >= spi->ctx.rx_len) {
		write_rxftlr(dev, spi->ctx.rx_len - 1);
	}
}

static int spi_dw_configure(const struct device *dev, struct spi_dw_data *spi,
			    const struct spi_config *config)
{
	const struct spi_dw_config *info = dev->config;
	uint32_t ctrlr0 = 0U;

	LOG_DBG("%p (prev %p)", config, spi->ctx.config);

	if (spi_context_configured(&spi->ctx, config)) {
		/* Nothing to do */
		return 0;
	}

	if (config->operation & SPI_HALF_DUPLEX) {
		LOG_ERR("Half-duplex not supported");
		return -ENOTSUP;
	}

	/* Verify if requested op mode is relevant to this controller */
	if (config->operation & SPI_OP_MODE_SLAVE) {
		if (!(info->serial_target)) {
			LOG_ERR("Slave mode not supported");
			return -ENOTSUP;
		}
	} else {
		if (info->serial_target) {
			LOG_ERR("Master mode not supported");
			return -ENOTSUP;
		}
	}

	if ((config->operation & SPI_TRANSFER_LSB) ||
	    (IS_ENABLED(CONFIG_SPI_EXTENDED_MODES) &&
	     (config->operation & (SPI_LINES_DUAL | SPI_LINES_QUAD | SPI_LINES_OCTAL)))) {
		LOG_ERR("Unsupported configuration");
		return -EINVAL;
	}

	if (info->max_xfer_size < SPI_WORD_SIZE_GET(config->operation)) {
		LOG_ERR("Max xfer size is %u, word size of %u not allowed", info->max_xfer_size,
			SPI_WORD_SIZE_GET(config->operation));
		return -ENOTSUP;
	}

	/* Word size */
	if (info->max_xfer_size == 32) {
		ctrlr0 |= DW_SPI_CTRLR0_DFS_32(SPI_WORD_SIZE_GET(config->operation));
	} else {
		ctrlr0 |= DW_SPI_CTRLR0_DFS_16(SPI_WORD_SIZE_GET(config->operation));
	}

	/* Determine how many bytes are required per-frame */
	spi->dfs = SPI_WS_TO_DFS(SPI_WORD_SIZE_GET(config->operation));

	/* SPI mode */
	if (SPI_MODE_GET(config->operation) & SPI_MODE_CPOL) {
		ctrlr0 |= DW_SPI_CTRLR0_SCPOL;
	}

	if (SPI_MODE_GET(config->operation) & SPI_MODE_CPHA) {
		ctrlr0 |= DW_SPI_CTRLR0_SCPH;
	}

	if (SPI_MODE_GET(config->operation) & SPI_MODE_LOOP) {
		ctrlr0 |= DW_SPI_CTRLR0_SRL;
	}

	/* Installing the configuration */
	write_ctrlr0(dev, ctrlr0);

	/* At this point, it's mandatory to set this on the context! */
	spi->ctx.config = config;

	if (!spi_dw_is_slave(spi)) {
		/* Baud rate and Slave select, for master only */
		write_baudr(dev, SPI_DW_CLK_DIVIDER(info->clock_frequency, config->frequency));
	}

	if (spi_dw_is_slave(spi)) {
		LOG_DBG("Installed slave config %p:"
			" ws/dfs %u/%u, mode %u/%u/%u",
			config, SPI_WORD_SIZE_GET(config->operation), spi->dfs,
			(SPI_MODE_GET(config->operation) & SPI_MODE_CPOL) ? 1 : 0,
			(SPI_MODE_GET(config->operation) & SPI_MODE_CPHA) ? 1 : 0,
			(SPI_MODE_GET(config->operation) & SPI_MODE_LOOP) ? 1 : 0);
	} else {
		LOG_DBG("Installed master config %p: freq %uHz (div = %u),"
			" ws/dfs %u/%u, mode %u/%u/%u, slave %u",
			config, config->frequency,
			SPI_DW_CLK_DIVIDER(info->clock_frequency, config->frequency),
			SPI_WORD_SIZE_GET(config->operation), spi->dfs,
			(SPI_MODE_GET(config->operation) & SPI_MODE_CPOL) ? 1 : 0,
			(SPI_MODE_GET(config->operation) & SPI_MODE_CPHA) ? 1 : 0,
			(SPI_MODE_GET(config->operation) & SPI_MODE_LOOP) ? 1 : 0, config->slave);
	}

	return 0;
}

static uint32_t spi_dw_compute_ndf(const struct spi_buf *rx_bufs, size_t rx_count, uint8_t dfs)
{
	uint32_t len = 0U;

	for (; rx_count; rx_bufs++, rx_count--) {
		if (len > (UINT16_MAX - rx_bufs->len)) {
			goto error;
		}

		len += rx_bufs->len;
	}

	if (len) {
		return (len / dfs) - 1;
	}
error:
	return UINT32_MAX;
}

static void spi_dw_update_txftlr(const struct device *dev, struct spi_dw_data *spi)
{
	const struct spi_dw_config *info = dev->config;
	uint32_t dw_spi_txftlr_dflt = (info->fifo_depth * 1) / 2;
	uint32_t reg_data = dw_spi_txftlr_dflt;

	if (spi_dw_is_slave(spi)) {
		if (!spi->ctx.tx_len) {
			reg_data = 0U;
		} else if (spi->ctx.tx_len < dw_spi_txftlr_dflt) {
			reg_data = spi->ctx.tx_len - 1;
		}
	}

	LOG_DBG("TxFTLR: %u", reg_data);

	write_txftlr(dev, reg_data);
}

static int transceive(const struct device *dev, const struct spi_config *config,
		      const struct spi_buf_set *tx_bufs, const struct spi_buf_set *rx_bufs,
		      bool asynchronous, spi_callback_t cb, void *userdata)
{
	const struct spi_dw_config *info = dev->config;
	struct spi_dw_data *spi = dev->data;
	uint32_t tmod = DW_SPI_CTRLR0_TMOD_TX_RX;
	uint32_t dw_spi_rxftlr_dflt = (info->fifo_depth * 5) / 8;
	uint32_t reg_data;
	int ret;
#ifdef CONFIG_SPI_DW_DMA
	bool use_dma = false;
#endif

	spi_context_lock(&spi->ctx, asynchronous, cb, userdata, config);

#ifdef CONFIG_PM_DEVICE
	if (!pm_device_is_busy(dev)) {
		pm_device_busy_set(dev);
	}
#endif /* CONFIG_PM_DEVICE */

	/* Configure */
	ret = spi_dw_configure(dev, spi, config);
	if (ret) {
		goto out;
	}

#ifdef CONFIG_SPI_DW_DMA
	/* check if should use DMA */
	use_dma = spi_dw_should_use_dma(info, tx_bufs, rx_bufs);
	if (use_dma) {
		LOG_DBG("Using DMA for SPI transfer");
		ret = spi_dw_configure_dma(dev, tx_bufs, rx_bufs, spi->dfs);
		if (ret) {
			LOG_WRN("DMA configuration failed, falling back to interrupt mode");
			use_dma = false;
		}
	} else {
		LOG_DBG("Not using DMA for SPI transfer");
	}
#endif

	if (!rx_bufs || !rx_bufs->buffers) {
		tmod = DW_SPI_CTRLR0_TMOD_TX;
	} else if (!tx_bufs || !tx_bufs->buffers) {
		tmod = DW_SPI_CTRLR0_TMOD_RX;
	}

	/* ToDo: add a way to determine EEPROM mode */

	if (tmod >= DW_SPI_CTRLR0_TMOD_RX && !spi_dw_is_slave(spi)) {
		reg_data = spi_dw_compute_ndf(rx_bufs->buffers, rx_bufs->count, spi->dfs);
		if (reg_data == UINT32_MAX) {
			ret = -EINVAL;
			goto out;
		}

		write_ctrlr1(dev, reg_data);
	} else {
		write_ctrlr1(dev, 0);
	}

	if (spi_dw_is_slave(spi)) {
		/* Enabling MISO line relevantly */
		if (tmod == DW_SPI_CTRLR0_TMOD_RX) {
			tmod |= DW_SPI_CTRLR0_SLV_OE;
		} else {
			tmod &= ~DW_SPI_CTRLR0_SLV_OE;
		}
	}

	/* Updating TMOD in CTRLR0 register */
	reg_data = read_ctrlr0(dev);
	reg_data &= ~DW_SPI_CTRLR0_TMOD_RESET;
	reg_data |= tmod;

	write_ctrlr0(dev, reg_data);

	/* Set buffers info */
	spi_context_buffers_setup(&spi->ctx, tx_bufs, rx_bufs, spi->dfs);

	spi->fifo_diff = 0U;

	/* Tx Threshold */
	spi_dw_update_txftlr(dev, spi);

	/* Does Rx thresholds needs to be lower? */
	reg_data = dw_spi_rxftlr_dflt;

	if (spi_dw_is_slave(spi)) {
		if (spi->ctx.rx_len && spi->ctx.rx_len < dw_spi_rxftlr_dflt) {
			reg_data = spi->ctx.rx_len - 1;
		}
	} else {
		if (spi->ctx.rx_len && spi->ctx.rx_len < info->fifo_depth) {
			reg_data = spi->ctx.rx_len - 1;
		}
	}

	/* Rx Threshold */
	write_rxftlr(dev, reg_data);

#ifdef CONFIG_SPI_DW_DMA
	if (use_dma) {
		uint32_t dma_threshold = info->fifo_depth / 2 - 1;
		uint32_t dma_cr = 0;

		/* Per DW_apb_ssi databook, the DMATDLR and DMARDLR should be
		 * configured when the SSI is disabled.
		 */
		if (has_tx_data(tx_bufs)) {
			dma_cr |= 0x2; /* TDMAE */
			write_dmatdlr(dev, dma_threshold);
		}

		if (has_rx_data(rx_bufs)) {
			dma_cr |= 0x1; /* RDMAE */
			write_dmardlr(dev, dma_threshold);
		}

		LOG_DBG("DMA configuration: DMACR=0x%02x, threshold=%d", dma_cr, dma_threshold);

		/* DMA mode, disable all interrupts */
		write_imr(dev, DW_SPI_IMR_MASK);
		write_dmacr(dev, dma_cr);
	} else {
		/* Enable interrupts for non-DMA mode */
		reg_data = rx_bufs ? DW_SPI_IMR_UNMASK : (DW_SPI_IMR_UNMASK & DW_SPI_IMR_MASK_RX);
		write_imr(dev, reg_data);
	}
#else
	/* Enable interrupts (DMA not available) */
	reg_data = rx_bufs ? DW_SPI_IMR_UNMASK : (DW_SPI_IMR_UNMASK & DW_SPI_IMR_MASK_RX);
	write_imr(dev, reg_data);
#endif

	if (!spi_dw_is_slave(spi)) {
		/* if cs is not defined as gpio, use hw cs */
		if (spi_cs_is_gpio(config)) {
			spi_context_cs_control(&spi->ctx, true);
#ifdef CONFIG_SPI_DW_FMQL_EMIO_CS
			/* note: if use emio cs, need to set ser register */
			write_ser(dev, BIT(config->slave));
#endif
		} else {
			write_ser(dev, BIT(config->slave));
		}
	}

	LOG_DBG("Enabling controller");
	set_bit_ssienr(dev);

#ifdef CONFIG_SPI_DW_DMA
	if (use_dma) {
		/* start DMA transfer */
		LOG_DBG("Starting DMA transfer");
		ret = spi_dw_start_dma(dev, tx_bufs, rx_bufs);
		if (ret) {
			LOG_ERR("Failed to start DMA transfer: %d", ret);
			goto out;
		}

		/* wait for DMA transfer to complete */
		ret = k_sem_take(&spi->dma_sem, K_MSEC(500));
		if (ret) {
			LOG_ERR("DMA transfer timeout");
			ret = -ETIMEDOUT;
			goto out;
		}

		/* check if there is DMA error */
		if (spi->dma_stat & SPI_DW_DMA_ERROR_FLAG) {
			LOG_ERR("DMA transfer error");
			ret = -EIO;
			goto out;
		}

		/* Add delay to ensure DMA transfer completion before stopping channels.
		 * Direct checking of DW_CFGL_FIFO_EMPTY flag is unreliable because:
		 * 1. The flag reflects instantaneous FIFO state, not transfer completion
		 * 2. Hardware requires time to complete current burst and update status
		 * This delay ensures all pending transfers are flushed before channel stop.
		 */
		k_busy_wait(100);
		spi_dw_stop_dma_channels(dev, tx_bufs, rx_bufs);

		/* Invalidate cache after RX DMA completion to ensure CPU sees DMA data */
#ifdef CONFIG_DCACHE
		if (has_rx_data(rx_bufs)) {
			sys_cache_data_invd_range(rx_bufs->buffers[0].buf, rx_bufs->buffers[0].len);
			LOG_DBG("Invalidated RX cache after DMA completion: addr=0x%p, size=%d",
				rx_bufs->buffers[0].buf, rx_bufs->buffers[0].len);
		}
#endif

		/* DMA transfer completed, clean SPI controller state */
		completed(dev, 0);
	} else {
		ret = spi_context_wait_for_completion(&spi->ctx);
	}
#else
	ret = spi_context_wait_for_completion(&spi->ctx);

#ifdef CONFIG_SPI_SLAVE
	if (spi_context_is_slave(&spi->ctx) && !ret) {
		ret = spi->ctx.recv_frames;
	}
#endif /* CONFIG_SPI_SLAVE */
#endif /* CONFIG_SPI_DW_DMA */

out:
	spi_context_release(&spi->ctx, ret);

	pm_device_busy_clear(dev);

	return ret;
}

static int spi_dw_transceive(const struct device *dev, const struct spi_config *config,
			     const struct spi_buf_set *tx_bufs, const struct spi_buf_set *rx_bufs)
{
	LOG_DBG("%p, %p, %p", dev, tx_bufs, rx_bufs);

	return transceive(dev, config, tx_bufs, rx_bufs, false, NULL, NULL);
}

#ifdef CONFIG_SPI_ASYNC
static int spi_dw_transceive_async(const struct device *dev, const struct spi_config *config,
				   const struct spi_buf_set *tx_bufs,
				   const struct spi_buf_set *rx_bufs, spi_callback_t cb,
				   void *userdata)
{
	LOG_DBG("%p, %p, %p, %p, %p", dev, tx_bufs, rx_bufs, cb, userdata);

	return transceive(dev, config, tx_bufs, rx_bufs, true, cb, userdata);
}
#endif /* CONFIG_SPI_ASYNC */

static int spi_dw_release(const struct device *dev, const struct spi_config *config)
{
	struct spi_dw_data *spi = dev->data;

	if (!spi_context_configured(&spi->ctx, config)) {
		return -EINVAL;
	}

	spi_context_unlock_unconditionally(&spi->ctx);

	return 0;
}

void spi_dw_isr(const struct device *dev)
{
	uint32_t int_status;
	int error;

	int_status = read_isr(dev);

	LOG_DBG("SPI %p int_status 0x%x - (tx: %d, rx: %d)", dev, int_status, read_txflr(dev),
		read_rxflr(dev));

	if (int_status & DW_SPI_ISR_ERRORS_MASK) {
		LOG_ERR("DW_SPI_ISR_ERRORS_MASK, status: 0x%x", int_status);
		error = -EIO;
		goto out;
	}

	error = 0;

	if (int_status & DW_SPI_ISR_RXFIS) {
		pull_data(dev);
	}

	if (int_status & DW_SPI_ISR_TXEIS) {
		push_data(dev);
	}

out:
	clear_interrupts(dev);
	completed(dev, error);
}

static const struct spi_driver_api dw_spi_api = {
	.transceive = spi_dw_transceive,
#ifdef CONFIG_SPI_ASYNC
	.transceive_async = spi_dw_transceive_async,
#endif /* CONFIG_SPI_ASYNC */
#ifdef CONFIG_SPI_RTIO
	.iodev_submit = spi_rtio_iodev_default_submit,
#endif
	.release = spi_dw_release,
};

int spi_dw_init(const struct device *dev)
{
	int err;
	const struct spi_dw_config *info = dev->config;
	struct spi_dw_data *spi = dev->data;

#ifdef CONFIG_PINCTRL
	pinctrl_apply_state(info->pcfg, PINCTRL_STATE_DEFAULT);
#endif

	DEVICE_MMIO_MAP(dev, K_MEM_CACHE_NONE);

	info->config_func();

	/* Masking interrupt and making sure controller is disabled */
	write_imr(dev, DW_SPI_IMR_MASK);
	clear_bit_ssienr(dev);

	LOG_DBG("Designware SPI driver initialized on device: %p", dev);

#ifdef CONFIG_SPI_DW_DMA
	k_sem_init(&spi->dma_sem, 0, 1);

	/* check if DMA device is ready */
	if (info->dma_tx.dma_dev && !device_is_ready(info->dma_tx.dma_dev)) {
		LOG_ERR("TX DMA device not ready");
		return -ENODEV;
	}

	if (info->dma_rx.dma_dev && !device_is_ready(info->dma_rx.dma_dev)) {
		LOG_ERR("RX DMA device not ready");
		return -ENODEV;
	}

	LOG_DBG("DMA support initialized - TX: %s channel %d, RX: %s channel %d",
		info->dma_tx.dma_dev ? info->dma_tx.dma_dev->name : "none", info->dma_tx.channel,
		info->dma_rx.dma_dev ? info->dma_rx.dma_dev->name : "none", info->dma_rx.channel);
#endif

	err = spi_context_cs_configure_all(&spi->ctx);
	if (err < 0) {
		return err;
	}

	spi_context_unlock_unconditionally(&spi->ctx);

	return 0;
}

#define SPI_CFG_IRQS_SINGLE_ERR_LINE(inst)                                                         \
	IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, rx_avail, irq),                                      \
		    DT_INST_IRQ_BY_NAME(inst, rx_avail, priority), spi_dw_isr,                     \
		    DEVICE_DT_INST_GET(inst), 0);                                                  \
	IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, tx_req, irq),                                        \
		    DT_INST_IRQ_BY_NAME(inst, tx_req, priority), spi_dw_isr,                       \
		    DEVICE_DT_INST_GET(inst), 0);                                                  \
	IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, err_int, irq),                                       \
		    DT_INST_IRQ_BY_NAME(inst, err_int, priority), spi_dw_isr,                      \
		    DEVICE_DT_INST_GET(inst), 0);                                                  \
	irq_enable(DT_INST_IRQ_BY_NAME(inst, rx_avail, irq));                                      \
	irq_enable(DT_INST_IRQ_BY_NAME(inst, tx_req, irq));                                        \
	irq_enable(DT_INST_IRQ_BY_NAME(inst, err_int, irq));

#define SPI_CFG_IRQS_MULTIPLE_ERR_LINES(inst)                                                      \
	IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, rx_avail, irq),                                      \
		    DT_INST_IRQ_BY_NAME(inst, rx_avail, priority), spi_dw_isr,                     \
		    DEVICE_DT_INST_GET(inst), 0);                                                  \
	IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, tx_req, irq),                                        \
		    DT_INST_IRQ_BY_NAME(inst, tx_req, priority), spi_dw_isr,                       \
		    DEVICE_DT_INST_GET(inst), 0);                                                  \
	IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, txo_err, irq),                                       \
		    DT_INST_IRQ_BY_NAME(inst, txo_err, priority), spi_dw_isr,                      \
		    DEVICE_DT_INST_GET(inst), 0);                                                  \
	IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, rxo_err, irq),                                       \
		    DT_INST_IRQ_BY_NAME(inst, rxo_err, priority), spi_dw_isr,                      \
		    DEVICE_DT_INST_GET(inst), 0);                                                  \
	IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, rxu_err, irq),                                       \
		    DT_INST_IRQ_BY_NAME(inst, rxu_err, priority), spi_dw_isr,                      \
		    DEVICE_DT_INST_GET(inst), 0);                                                  \
	IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, mst_err, irq),                                       \
		    DT_INST_IRQ_BY_NAME(inst, mst_err, priority), spi_dw_isr,                      \
		    DEVICE_DT_INST_GET(inst), 0);                                                  \
	irq_enable(DT_INST_IRQ_BY_NAME(inst, rx_avail, irq));                                      \
	irq_enable(DT_INST_IRQ_BY_NAME(inst, tx_req, irq));                                        \
	irq_enable(DT_INST_IRQ_BY_NAME(inst, txo_err, irq));                                       \
	irq_enable(DT_INST_IRQ_BY_NAME(inst, rxo_err, irq));                                       \
	irq_enable(DT_INST_IRQ_BY_NAME(inst, rxu_err, irq));                                       \
	irq_enable(DT_INST_IRQ_BY_NAME(inst, mst_err, irq));

#define SPI_DW_IRQ_HANDLER(inst)                                                                   \
	void spi_dw_irq_config_##inst(void)                                                        \
	{                                                                                          \
		COND_CODE_1(IS_EQ(DT_NUM_IRQS(DT_DRV_INST(inst)), 1),                              \
			    (IRQ_CONNECT(DT_INST_IRQN(inst), DT_INST_IRQ(inst, priority),          \
					 spi_dw_isr, DEVICE_DT_INST_GET(inst), 0);                 \
			     irq_enable(DT_INST_IRQN(inst));),                                     \
			    (COND_CODE_1(IS_EQ(DT_NUM_IRQS(DT_DRV_INST(inst)), 3),                 \
					 (SPI_CFG_IRQS_SINGLE_ERR_LINE(inst)),                     \
					 (SPI_CFG_IRQS_MULTIPLE_ERR_LINES(inst)))))                \
	}

#ifdef CONFIG_SPI_DW_DMA
#define SPI_DW_DMA_CHANNELS(inst)                                                                  \
	COND_CODE_1(                                                                               \
		DT_INST_DMAS_HAS_NAME(inst, tx),                                                   \
		(.dma_tx =                                                                         \
			 {                                                                         \
				 .dma_dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(inst, tx)),    \
				 .channel = DT_INST_DMAS_CELL_BY_NAME(inst, tx, channel),          \
				 .dma_cfg =                                                        \
					 {                                                         \
						 .channel_direction = MEMORY_TO_PERIPHERAL,        \
						 .dma_callback = spi_dw_dma_callback,              \
						 .source_burst_length = 1,                         \
						 .dest_burst_length = 1,                           \
						 .source_data_size = 1,                            \
						 .dest_data_size = 1,                              \
						 .block_count = 1,                                 \
						 .dma_slot = DT_INST_DMAS_CELL_BY_NAME(inst, tx,   \
										       slot),      \
						 .source_handshake = 0,                            \
						 .dest_handshake = 1,                              \
					 },                                                        \
			 }, ),                                                                     \
		(.dma_tx = {.dma_dev = NULL}, ))                                                   \
	COND_CODE_1(                                                                               \
		DT_INST_DMAS_HAS_NAME(inst, rx),                                                   \
		(.dma_rx =                                                                         \
			 {                                                                         \
				 .dma_dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(inst, rx)),    \
				 .channel = DT_INST_DMAS_CELL_BY_NAME(inst, rx, channel),          \
				 .dma_cfg =                                                        \
					 {                                                         \
						 .channel_direction = PERIPHERAL_TO_MEMORY,        \
						 .dma_callback = spi_dw_dma_callback,              \
						 .source_burst_length = 1,                         \
						 .dest_burst_length = 1,                           \
						 .source_data_size = 1,                            \
						 .dest_data_size = 1,                              \
						 .block_count = 1,                                 \
						 .dma_slot = DT_INST_DMAS_CELL_BY_NAME(inst, rx,   \
										       slot),      \
						 .source_handshake = 1,                            \
						 .dest_handshake = 0,                              \
					 },                                                        \
			 }, ),                                                                     \
		(.dma_rx = {.dma_dev = NULL}, ))
#else
#define SPI_DW_DMA_CHANNELS(inst)
#endif

#define SPI_DW_INIT(inst)                                                                          \
	IF_ENABLED(CONFIG_PINCTRL, (PINCTRL_DT_INST_DEFINE(inst);))                                \
	SPI_DW_IRQ_HANDLER(inst);                                                                  \
	static struct spi_dw_data spi_dw_data_##inst = {                                           \
		SPI_CONTEXT_INIT_LOCK(spi_dw_data_##inst, ctx),                                    \
		SPI_CONTEXT_INIT_SYNC(spi_dw_data_##inst, ctx),                                    \
		SPI_CONTEXT_CS_GPIOS_INITIALIZE(DT_DRV_INST(inst), ctx)};                          \
	static const struct spi_dw_config spi_dw_config_##inst = {                                 \
		DEVICE_MMIO_ROM_INIT(DT_DRV_INST(inst)),                                           \
		.clock_frequency = COND_CODE_1(                                                    \
			DT_NODE_HAS_PROP(DT_INST_PHANDLE(inst, clocks), clock_frequency),          \
			(DT_INST_PROP_BY_PHANDLE(inst, clocks, clock_frequency)),                  \
			(DT_INST_PROP(inst, clock_frequency))),                                    \
		.config_func = spi_dw_irq_config_##inst,                                           \
		.serial_target = DT_INST_PROP(inst, serial_target),                                \
		.fifo_depth = DT_INST_PROP(inst, fifo_depth),                                      \
		.max_xfer_size = DT_INST_PROP(inst, max_xfer_size),                                \
		IF_ENABLED(CONFIG_PINCTRL, (.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst), ))       \
			SPI_DW_DMA_CHANNELS(inst) COND_CODE_1(                                     \
				DT_INST_PROP(inst, aux_reg),                                       \
				(.read_func = aux_reg_read, .write_func = aux_reg_write,           \
				 .set_bit_func = aux_reg_set_bit,                                  \
				 .clear_bit_func = aux_reg_clear_bit,                              \
				 .test_bit_func = aux_reg_test_bit, ),                             \
				(.read_func = reg_read, .write_func = reg_write,                   \
				 .set_bit_func = reg_set_bit, .clear_bit_func = reg_clear_bit,     \
				 .test_bit_func = reg_test_bit, ))};                               \
	DEVICE_DT_INST_DEFINE(inst, spi_dw_init, NULL, &spi_dw_data_##inst, &spi_dw_config_##inst, \
			      POST_KERNEL, CONFIG_SPI_INIT_PRIORITY, &dw_spi_api);

DT_INST_FOREACH_STATUS_OKAY(SPI_DW_INIT)
