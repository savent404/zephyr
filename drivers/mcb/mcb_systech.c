/**
 * @file mcb_systech.c
 * @author Liao YuanKai (savent_gate@outlook.com)
 * @brief
 * @version 0.1
 * @date 2025-01-07
 *
 * @copyright Copyright (c) 2025
 * @license This project is CLOSED SOURCE, All Rights Reserved
 *
 */
#include <zephyr/drivers/mcb.h>

#define LOG_LEVEL CONFIG_MCB_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mcb_systech);

#define DT_DRV_COMPAT systech_mcb

struct mcb_systech_data {
};

struct mcb_systech_config {
	/* Region 0 */
	uintptr_t reg_base;
	size_t reg_size;
	/* Region 1 */
	uintptr_t tx_base;
	size_t tx_size;
	/* Region 2 */
	uintptr_t rx_base;
	size_t rx_size;
};

void mcb_systech_reset(const struct device *dev, uint8_t role)
{
	const struct mcb_systech_config *config = dev->config;
	struct mcb_systech_data *data = dev->data;

	ARG_UNUSED(data);
	ARG_UNUSED(config);
}

void mcb_systech_poll_time(const struct device *dev, uint32_t timeout)
{
	const struct mcb_systech_config *config = dev->config;
	struct mcb_systech_data *data = dev->data;

	ARG_UNUSED(data);
	ARG_UNUSED(config);
}

void mcb_systech_config_port(const struct device *dev, uint8_t port, bool enable, bool write,
			     uint16_t max_rx_len)
{
	const struct mcb_systech_config *config = dev->config;
	struct mcb_systech_data *data = dev->data;

	ARG_UNUSED(data);
	ARG_UNUSED(config);
}

void mcb_systech_get_status(const struct device *dev, uint32_t *status)
{
	const struct mcb_systech_config *config = dev->config;
	struct mcb_systech_data *data = dev->data;

	ARG_UNUSED(data);
	ARG_UNUSED(config);

	*status = _MCB_ERR_T_ERR | _MCB_ERR_R_ERR;
}

void mcb_systech_clr_status(const struct device *dev, uint32_t bits)
{
	const struct mcb_systech_config *config = dev->config;
	struct mcb_systech_data *data = dev->data;

	ARG_UNUSED(data);
	ARG_UNUSED(config);
}

void *mcb_systech_get_rx_buf(const struct device *dev, uint8_t port)
{
	const struct mcb_systech_config *config = dev->config;
	struct mcb_systech_data *data = dev->data;

	ARG_UNUSED(data);
	ARG_UNUSED(config);

	return NULL;
}

void *mcb_systech_get_tx_buf(const struct device *dev, uint8_t port)
{
	const struct mcb_systech_config *config = dev->config;
	struct mcb_systech_data *data = dev->data;

	ARG_UNUSED(data);
	ARG_UNUSED(config);

	return NULL;
}

uint16_t mcb_systech_get_rx_len(const struct device *dev, uint8_t port)
{
	const struct mcb_systech_config *config = dev->config;
	struct mcb_systech_data *data = dev->data;

	ARG_UNUSED(data);
	ARG_UNUSED(config);

	return 0;
}

uint16_t mcb_systech_get_tx_len(const struct device *dev, uint8_t port)
{
	const struct mcb_systech_config *config = dev->config;
	struct mcb_systech_data *data = dev->data;

	ARG_UNUSED(data);
	ARG_UNUSED(config);

	return 0;
}

void mcb_systech_set_tx_len(const struct device *dev, uint8_t port, uint16_t len)
{
	const struct mcb_systech_config *config = dev->config;
	struct mcb_systech_data *data = dev->data;

	ARG_UNUSED(data);
	ARG_UNUSED(config);
}

void mcb_systech_rx_clr(const struct device *dev, uint8_t port)
{
	const struct mcb_systech_config *config = dev->config;
	struct mcb_systech_data *data = dev->data;

	ARG_UNUSED(data);
	ARG_UNUSED(config);
}

int mcb_systech_rx_is_ready(const struct device *dev, uint8_t port)
{
	const struct mcb_systech_config *config = dev->config;
	struct mcb_systech_data *data = dev->data;

	ARG_UNUSED(data);
	ARG_UNUSED(config);

	return 0;
}

static const struct mcb_driver_api mcb_systech_api = {
	.reset = mcb_systech_reset,
};

static int mcb_systech_init(const struct device *dev)
{
	const struct mcb_systech_config *config = dev->config;
	struct mcb_systech_data *data = dev->data;

	ARG_UNUSED(data);
	ARG_UNUSED(config);

	/* FIXME: Do the MMIO mapping here */

	LOG_DBG("MCB init");

	return 0;
}

#define MCB_SYSTECH_DEVICE(n)                                                                      \
	static struct mcb_systech_data mcb_systech_data_##n;                                       \
	static const struct mcb_systech_config mcb_systech_config_##n = {                          \
		.reg_base = DT_INST_REG_ADDR_BY_IDX(n, 0),                                         \
		.reg_size = DT_INST_REG_SIZE_BY_IDX(n, 0),                                         \
		.tx_base = DT_INST_REG_ADDR_BY_IDX(n, 1),                                          \
		.tx_size = DT_INST_REG_SIZE_BY_IDX(n, 1),                                          \
		.rx_base = DT_INST_REG_ADDR_BY_IDX(n, 2),                                          \
		.rx_size = DT_INST_REG_SIZE_BY_IDX(n, 2),                                          \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, &mcb_systech_init, NULL, &mcb_systech_data_##n,                   \
			      &mcb_systech_config_##n, POST_KERNEL, CONFIG_MCB_INIT_PRIORITY,      \
			      &mcb_systech_api);
DT_INST_FOREACH_STATUS_OKAY(MCB_SYSTECH_DEVICE)
