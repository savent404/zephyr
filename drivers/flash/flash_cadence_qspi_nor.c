/*
 * Copyright (c) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT cdns_qspi_nor

#include "flash_cadence_qspi_nor_ll.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(flash_cadence, CONFIG_FLASH_LOG_LEVEL);

struct flash_cad_priv {
	DEVICE_MMIO_NAMED_RAM(qspi_reg);
	DEVICE_MMIO_NAMED_RAM(qspi_data);
	struct cad_qspi_params params;
};

struct flash_cad_config {
	DEVICE_MMIO_NAMED_ROM(qspi_reg);
	DEVICE_MMIO_NAMED_ROM(qspi_data);
#ifdef CONFIG_QSPI_DW_DMA
	const struct device *dma_tx_dev;
	const struct device *dma_rx_dev;
	uint32_t dma_tx_channel;
	uint32_t dma_rx_channel;
	uint32_t dma_tx_slot;
	uint32_t dma_rx_slot;
#endif
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	const struct flash_pages_layout *pages_layout;
	size_t pages_layout_size;
#endif
};

static const struct flash_parameters flash_cad_parameters = {
	.write_block_size = QSPI_BYTES_PER_DEV,
	.erase_value = 0xff,
};

#define DEV_DATA(dev) ((struct flash_cad_priv *)((dev)->data))
#define DEV_CFG(dev)  ((struct flash_cad_config *)((dev)->config))

static int flash_cad_read(const struct device *dev, off_t offset, void *data, size_t len)
{
	struct flash_cad_priv *priv = dev->data;
	struct cad_qspi_params *cad_params = &priv->params;
	int rc;

	if ((data == NULL) || (len == 0)) {
		LOG_ERR("Invalid input parameter for QSPI Read!");
		return -EINVAL;
	}

#ifdef CONFIG_QSPI_DW_DMA
	/* Try DMA read first if available */
	if (cad_params->dma_rx_dev && cad_params->dma_tx_dev) {
		rc = cad_qspi_dma_read(cad_params, data, (uint32_t)offset, len);
	} else {
		rc = cad_qspi_read(cad_params, data, (uint32_t)offset, len);
	}
#else
	rc = cad_qspi_read(cad_params, data, (uint32_t)offset, len);
#endif

	if (rc < 0) {
		LOG_ERR("Cadence QSPI Flash Read Failed");
		return rc;
	}

	return 0;
}

static int flash_cad_erase(const struct device *dev, off_t offset, size_t len)
{
	struct flash_cad_priv *priv = dev->data;
	struct cad_qspi_params *cad_params = &priv->params;
	int rc;

	if (len == 0) {
		LOG_ERR("Invalid input parameter for QSPI Erase!");
		return -EINVAL;
	}

	rc = cad_qspi_erase(cad_params, (uint32_t)offset, len);

	if (rc < 0) {
		LOG_ERR("Cadence QSPI Flash Erase Failed!");
		return rc;
	}

	return 0;
}

static int flash_cad_write(const struct device *dev, off_t offset, const void *data, size_t len)
{
	struct flash_cad_priv *priv = dev->data;
	struct cad_qspi_params *cad_params = &priv->params;
	int rc;

	if ((data == NULL) || (len == 0)) {
		LOG_ERR("Invalid input parameter for QSPI Write!");
		return -EINVAL;
	}

#ifdef CONFIG_QSPI_DW_DMA
	/* Try DMA write first if available */
	if (cad_params->dma_rx_dev && cad_params->dma_tx_dev) {
		rc = cad_qspi_dma_write(cad_params, (void *)data, (uint32_t)offset, len);
	} else {
		rc = cad_qspi_write(cad_params, (void *)data, (uint32_t)offset, len);
	}
#else
	rc = cad_qspi_write(cad_params, (void *)data, (uint32_t)offset, len);
#endif

	if (rc < 0) {
		LOG_ERR("Cadence QSPI Flash Write Failed!");
		return rc;
	}

	return 0;
}

static const struct flash_parameters *flash_cad_get_parameters(const struct device *dev)
{
	ARG_UNUSED(dev);

	return &flash_cad_parameters;
}

#if defined(CONFIG_FLASH_PAGE_LAYOUT)
static void flash_cad_pages_layout(const struct device *dev,
				   const struct flash_pages_layout **layout, size_t *layout_size)
{

	*layout = DEV_CFG(dev)->pages_layout;
	*layout_size = DEV_CFG(dev)->pages_layout_size;
}
#endif

static const struct flash_driver_api flash_cad_api = {
	.erase = flash_cad_erase,
	.write = flash_cad_write,
	.read = flash_cad_read,
	.get_parameters = flash_cad_get_parameters,
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	.page_layout = flash_cad_pages_layout,
#endif
};

static int flash_cad_init(const struct device *dev)
{
	struct flash_cad_priv *priv = dev->data;
	struct cad_qspi_params *cad_params = &priv->params;
#ifdef CONFIG_QSPI_DW_DMA
	const struct flash_cad_config *config = dev->config;
#endif
	int rc;

	DEVICE_MMIO_NAMED_MAP(dev, qspi_reg, K_MEM_CACHE_NONE);
	DEVICE_MMIO_NAMED_MAP(dev, qspi_data, K_MEM_CACHE_NONE);

	cad_params->reg_base = DEVICE_MMIO_NAMED_GET(dev, qspi_reg);
	cad_params->data_base = DEVICE_MMIO_NAMED_GET(dev, qspi_data);

#ifdef CONFIG_QSPI_DW_DMA
	/* Initialize DMA configuration */
	cad_params->dma_tx_dev = config->dma_tx_dev;
	cad_params->dma_rx_dev = config->dma_rx_dev;
	cad_params->dma_tx_channel = config->dma_tx_channel;
	cad_params->dma_rx_channel = config->dma_rx_channel;
	cad_params->dma_tx_slot = config->dma_tx_slot;
	cad_params->dma_rx_slot = config->dma_rx_slot;

	/* Initialize DMA if devices are available */
	if (cad_params->dma_tx_dev && cad_params->dma_rx_dev) {
		rc = cad_qspi_dma_init(cad_params);
		if (rc < 0) {
			LOG_WRN("DMA initialization failed, DMA disabled: %d", rc);
		} else {
			LOG_INF("DMA support enabled - TX: %s ch%d, RX: %s ch%d",
				cad_params->dma_tx_dev->name, cad_params->dma_tx_channel,
				cad_params->dma_rx_dev->name, cad_params->dma_rx_channel);
		}
	}
#endif

	rc = cad_qspi_init(cad_params, cad_params->cpha, cad_params->cpol, QSPI_CONFIG_CSDA,
			   QSPI_CONFIG_CSDADS, QSPI_CONFIG_CSEOT, QSPI_CONFIG_CSSOT, 0);

	if (rc < 0) {
		LOG_ERR("Cadence QSPI Flash Init Failed");
		return rc;
	}

	return 0;
}

#ifdef CONFIG_QSPI_DW_DMA
#define QSPI_DMA_CONFIG(inst)                                                                      \
	COND_CODE_1(DT_INST_DMAS_HAS_NAME(inst, tx),                                               \
		    (.dma_tx_dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(inst, tx)),             \
		     .dma_tx_channel = DT_INST_DMAS_CELL_BY_NAME(inst, tx, channel),               \
		     .dma_tx_slot = DT_INST_DMAS_CELL_BY_NAME(inst, tx, slot), ),                  \
		    (.dma_tx_dev = NULL, .dma_tx_channel = 0, .dma_tx_slot = 0, ))                 \
	COND_CODE_1(DT_INST_DMAS_HAS_NAME(inst, rx),                                               \
		    (.dma_rx_dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(inst, rx)),             \
		     .dma_rx_channel = DT_INST_DMAS_CELL_BY_NAME(inst, rx, channel),               \
		     .dma_rx_slot = DT_INST_DMAS_CELL_BY_NAME(inst, rx, slot)),                    \
		    (.dma_rx_dev = NULL, .dma_rx_channel = 0, .dma_rx_slot = 0))
#else
#define QSPI_DMA_CONFIG(inst)
#endif

#define CREATE_FLASH_CONFIG(inst)                                                                  \
	static struct flash_cad_config flash_cad_config_##inst = {                                 \
		DEVICE_MMIO_NAMED_ROM_INIT_BY_NAME(qspi_reg, DT_DRV_INST(inst)),                   \
		DEVICE_MMIO_NAMED_ROM_INIT_BY_NAME(qspi_data, DT_DRV_INST(inst)),                  \
		QSPI_DMA_CONFIG(inst)};

#define CREATE_FLASH_CONFIG_WITH_PAGES_LAYOUT(inst)                                                \
	const static struct flash_pages_layout flash_pages_layout_##inst[] = {                     \
		{.pages_count = DT_INST_PROP(inst, flat_page_count),                               \
		 .pages_size = DT_INST_PROP(inst, flat_page_size)}};                               \
	static struct flash_cad_config flash_cad_config_##inst = {                                 \
		DEVICE_MMIO_NAMED_ROM_INIT_BY_NAME(qspi_reg, DT_DRV_INST(inst)),                   \
		DEVICE_MMIO_NAMED_ROM_INIT_BY_NAME(qspi_data, DT_DRV_INST(inst)),                  \
		.pages_layout = flash_pages_layout_##inst,                                         \
		.pages_layout_size = ARRAY_SIZE(flash_pages_layout_##inst),                        \
		QSPI_DMA_CONFIG(inst)};

#define CREATE_FLASH_CADENCE_QSPI_DEVICE(inst)                                                     \
	static struct flash_cad_priv flash_cad_priv_##inst = {                                     \
		.params =                                                                          \
			{                                                                          \
				.clk_rate = DT_INST_PROP(inst, clock_frequency),                   \
				.data_size = DT_INST_REG_SIZE_BY_IDX(inst, 1),                     \
				.cpol = DT_INST_PROP_OR(inst, cpol, 0),                            \
				.cpha = DT_INST_PROP_OR(inst, cpha, 0),                            \
				.sram_fifo_size = DT_INST_PROP_OR(inst, sram_fifo_size, 1024),     \
			},                                                                         \
	};                                                                                         \
	COND_CODE_1(CONFIG_FLASH_PAGE_LAYOUT, (CREATE_FLASH_CONFIG_WITH_PAGES_LAYOUT(inst)),       \
		    (CREATE_FLASH_CONFIG(inst)));                                                  \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, flash_cad_init, NULL, &flash_cad_priv_##inst,                  \
			      &flash_cad_config_##inst, POST_KERNEL,                               \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &flash_cad_api);

DT_INST_FOREACH_STATUS_OKAY(CREATE_FLASH_CADENCE_QSPI_DEVICE)
