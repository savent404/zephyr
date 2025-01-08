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
#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/types.h>
#include <zephyr/drivers/mcb.h>
#include "mcb_systech.h"

#define LOG_LEVEL CONFIG_MCB_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mcb_systech);

#define DT_DRV_COMPAT systech_mcb

#define DEV_CFG(_dev)  ((const struct mcb_systech_config *const)(_dev)->config)
#define DEV_DATA(_dev) ((struct mcb_systech_data *const)(_dev)->data)

struct mcb_systech_data {
	DEVICE_MMIO_NAMED_RAM(reg);
	DEVICE_MMIO_NAMED_RAM(rx_buf);
	DEVICE_MMIO_NAMED_RAM(tx_buf);
	uint32_t bandwidth;
	uint32_t pool_time;
};

struct mcb_systech_config {
	DEVICE_MMIO_NAMED_ROM(reg);
	DEVICE_MMIO_NAMED_ROM(rx_buf);
	DEVICE_MMIO_NAMED_ROM(tx_buf);
	uint32_t max_bandwidth;
};

void mcb_systech_reset(const struct device *dev, uint8_t role)
{
	uint32_t reg_base = DEVICE_MMIO_NAMED_GET(dev, reg);
	uint32_t val;

	/* Clear CTRL/STATUS register */
	sys_write32(0, reg_base + MCB_REG_CTRL1);
	sys_write32(0, reg_base + MCB_REG_CTRL2);
	sys_write32(0, reg_base + MCB_REG_STATUS1);
	sys_write32(0, reg_base + MCB_REG_I_A_COUNT);
	sys_write32(0, reg_base + MCB_REG_I_B_COUNT);

	for (uint32_t addr = MCB_REG_PORT_MASK0; addr <= MCB_REG_PORT_MASK3; addr += 4) {
		sys_write32(0, reg_base + addr);
	}

	for (uint32_t addr = MCB_REG_PORT_RDY_MASK0; addr <= MCB_REG_PORT_RDY_MASK3; addr += 4) {
		sys_write32(0, reg_base + addr);
	}

	for (uint32_t addr = MCB_REG_PORT_W_MASK0; addr <= MCB_REG_PORT_W_MASK3; addr += 4) {
		sys_write32(0, reg_base + addr);
	}

	for (uint32_t addr = MCB_REG_PORT_RX_LEN(0); addr <= MCB_REG_PORT_RX_LEN(0x80); addr += 4) {
		sys_write32(0, reg_base + addr);
	}

	for (uint32_t addr = MCB_REG_PORT_RX_MAX(0); addr <= MCB_REG_PORT_RX_MAX(0x80); addr += 4) {
		sys_write32(0, reg_base + addr);
	}

	for (uint32_t addr = MCB_REG_PORT_TX_LEN(0); addr <= MCB_REG_PORT_TX_LEN(0x80); addr += 4) {
		sys_write32(0, reg_base + addr);
	}

	for (uint32_t addr = MCB_REG_PORT_RX_SID(0); addr <= MCB_REG_PORT_RX_SID(0x80); addr += 4) {
		sys_write32(0, reg_base + addr);
	}

	/* FIXME: Set DT and DR via DeviceTree or user configuration */
	switch (role) {
	case _MCB_ROLE_MASTER:
		val = VALUE2REG(CTRL2, DR, DR_MPU_P) | VALUE2REG(CTRL2, DT, DT_MPU_P);
		LOG_DBG("Set MCB to master mode");
		break;
	case _MCB_ROLE_SLAVE:
	default:
		val = VALUE2REG(CTRL2, DR, DR_MPU_B) | VALUE2REG(CTRL2, DT, DT_MPU_B);
		LOG_DBG("Set MCB to slave mode");
		break;
	}
	sys_write32(val, reg_base + MCB_REG_CTRL2);

	/* Enable RxEN as default */
	sys_write32(b_MCB_CTRL1_RxEN, reg_base + MCB_REG_CTRL1);
}

void mcb_systech_poll_time(const struct device *dev, uint32_t timeout)
{
	uint32_t reg_base = DEVICE_MMIO_NAMED_GET(dev, reg);
	struct mcb_systech_data *data = dev->data;
	uint32_t val;

	timeout = timeout / 10; /* Register POLL_TIME is in 10ns unit */
	data->pool_time = timeout;
	val = sys_read32(reg_base + MCB_REG_CTRL1);
	val &= ~_REG_MASK(CTRL2, PT);
	val |= VALUE2REG(CTRL2, PT, timeout);
	sys_write32(val, reg_base + MCB_REG_CTRL1);

	LOG_DBG("Set poll time to %dns", timeout * 10);
}

static inline void _config_port_general(const struct device *dev, uint8_t port, bool enable,
					bool write, uint32_t max_rx_len)
{
	uint32_t reg_base = DEVICE_MMIO_NAMED_GET(dev, reg);
	uint32_t val;
	uint32_t port_idx = port / 32;
	uint32_t port_bit = BIT(port % 32);
	/* setup none-critical parts */
	sys_write32(max_rx_len, reg_base + MCB_REG_PORT_RX_MAX(port));
	sys_write32(0, reg_base + MCB_REG_PORT_RX_SID(port));
	sys_write32(0, reg_base + MCB_REG_PORT_RX_LEN(port));
	sys_write32(0, reg_base + MCB_REG_PORT_TX_LEN(port));

	/* clear port ready mask */
	val = sys_read32(reg_base + MCB_REG_PORT_RDY_MASK0 + port_idx * 4);
	val &= ~port_bit;
	sys_write32(val, reg_base + MCB_REG_PORT_RDY_MASK0 + port_idx * 4);

	/* reset write mask */
	val = sys_read32(reg_base + MCB_REG_PORT_W_MASK0 + port_idx * 4);
	val &= ~port_bit;
	val |= write ? port_bit : 0;
	sys_write32(val, reg_base + MCB_REG_PORT_W_MASK0 + port_idx * 4);

	/* at least wipe potemtial ldp_a_header */
	memset(mcb_get_tx_buf(dev, port), 0, 4);
	memset(mcb_get_rx_buf(dev, port), 0, 4);
	/* FIXME: Once buffer cached, need to flush cache */
}

void mcb_systech_config_port(const struct device *dev, uint8_t port, bool enable, bool write,
			     uint16_t max_rx_len)
{
	uint32_t reg_base = DEVICE_MMIO_NAMED_GET(dev, reg);
	uint32_t val;
	uint32_t port_idx = port / 32;
	uint32_t port_bit = BIT(port % 32);

	if (port >= MCB_MAX_PORT) {
		LOG_ERR("Invalid port number");
		return;
	}

	if (enable) {
		_config_port_general(dev, port, enable, write, max_rx_len);

		/* enable port */
		val = sys_read32(reg_base + MCB_REG_PORT_MASK0 + port_idx * 4);
		val |= port_bit;
		sys_write32(val, reg_base + MCB_REG_PORT_MASK0 + port_idx * 4);
	} else {
		/* disable port */
		val = sys_read32(reg_base + MCB_REG_PORT_MASK0 + port_idx * 4);
		val &= ~port_bit;
		sys_write32(val, reg_base + MCB_REG_PORT_MASK0 + port_idx * 4);

		_config_port_general(dev, port, enable, write, max_rx_len);
	}
}

void mcb_systech_tx(const struct device *dev, uint8_t sid, uint8_t port)
{
	uint32_t reg_base = DEVICE_MMIO_NAMED_GET(dev, reg);
	uint32_t val;

	if (port >= MCB_MAX_PORT) {
		LOG_ERR("Invalid port number");
		return;
	}

	val = sys_read32(reg_base + MCB_REG_CTRL1);
	val |= b_MCB_CTRL1_TxEN;
	val &= _REG_MASK(CTRL1, D_SID);
	val |= VALUE2REG(CTRL1, D_SID, sid);
	val &= _REG_MASK(CTRL1, PORT);
	val |= VALUE2REG(CTRL1, PORT, port);
	sys_write32(val, reg_base + MCB_REG_CTRL1);
}

void mcb_systech_get_status(const struct device *dev, uint32_t *s)
{
	uint32_t reg_base = DEVICE_MMIO_NAMED_GET(dev, reg);
	uint32_t val, status = 0;

	val = sys_read32(reg_base + MCB_REG_STATUS1);

	/* Do the mapping work */
	if (val & b_MCB_STATUS1_RE) {
		status |= _MCB_ERR_R_ERR;
	}
	if (val & b_MCB_STATUS1_TE) {
		status |= _MCB_ERR_T_ERR;
	}
	if (val & b_MCB_STATUS1_IE) {
		status |= _MCB_ERR_I_ERR;
	}
	if (val & b_MCB_STATUS1_RF) {
		status |= _MCB_ERR_PREEMPT;
	}
	if (val & b_MCB_STATUS1_PE) {
		status |= _MCB_ERR_P_ERR;
	}

	if (s) {
		*s = status;
	}
}

void mcb_systech_clr_status(const struct device *dev, uint32_t bits)
{
	uint32_t reg_base = DEVICE_MMIO_NAMED_GET(dev, reg);
	uint32_t val;

	val = sys_read32(reg_base + MCB_REG_STATUS1);

	if (bits & _MCB_ERR_R_ERR) {
		val &= ~b_MCB_STATUS1_RE;
	}
	if (bits & _MCB_ERR_T_ERR) {
		val &= ~b_MCB_STATUS1_TE;
	}
	if (bits & _MCB_ERR_I_ERR) {
		val &= ~b_MCB_STATUS1_IE;
	}
	if (bits & _MCB_ERR_PREEMPT) {
		val &= ~b_MCB_STATUS1_RF;
	}
	if (bits & _MCB_ERR_P_ERR) {
		val &= ~b_MCB_STATUS1_PE;
	}

	sys_write32(val, reg_base + MCB_REG_STATUS1);
}

void *mcb_systech_get_rx_buf(const struct device *dev, uint8_t port)
{
	uint32_t rx_buf_base = DEVICE_MMIO_NAMED_GET(dev, rx_buf);

	if (port >= MCB_MAX_PORT) {
		LOG_ERR("Invalid port number");
		return NULL;
	}

	return (void *)(rx_buf_base + MCB_REG_PORT_RX(port));
}

void *mcb_systech_get_tx_buf(const struct device *dev, uint8_t port)
{
	uint32_t tx_buf_base = DEVICE_MMIO_NAMED_GET(dev, tx_buf);

	if (port >= MCB_MAX_PORT) {
		LOG_ERR("Invalid port number");
		return NULL;
	}

	return (void *)(tx_buf_base + MCB_REG_PORT_TX(port));
}

uint16_t mcb_systech_get_rx_len(const struct device *dev, uint8_t port)
{
	uint32_t reg_base = DEVICE_MMIO_NAMED_GET(dev, reg);
	uint32_t val;

	if (port >= MCB_MAX_PORT) {
		LOG_ERR("Invalid port number");
		return 0;
	}
	val = sys_read32(reg_base + MCB_REG_PORT_RX_LEN(port));
	return val;
}

uint16_t mcb_systech_get_tx_len(const struct device *dev, uint8_t port)
{
	uint32_t reg_base = DEVICE_MMIO_NAMED_GET(dev, reg);
	uint32_t val;

	if (port >= MCB_MAX_PORT) {
		LOG_ERR("Invalid port number");
		return 0;
	}
	val = sys_read32(reg_base + MCB_REG_PORT_TX_LEN(port));
	return val;
}

void mcb_systech_set_tx_len(const struct device *dev, uint8_t port, uint16_t len)
{
	uint32_t reg_base = DEVICE_MMIO_NAMED_GET(dev, reg);

	if (port >= MCB_MAX_PORT) {
		LOG_ERR("Invalid port number");
		return;
	}
	sys_write32(len, reg_base + MCB_REG_PORT_TX_LEN(port));
}

void mcb_systech_rx_clr(const struct device *dev, uint8_t port)
{
	uint32_t reg_base = DEVICE_MMIO_NAMED_GET(dev, reg);
	uint32_t val;

	val = sys_read32(reg_base + MCB_REG_STATUS1);
	val &= ~b_MCB_STATUS1_RDY;
	sys_write32(val, reg_base + MCB_REG_STATUS1);

	if (port >= MCB_MAX_PORT) {
		LOG_ERR("Invalid port number");
		return;
	}
	val = sys_read32(reg_base + MCB_REG_PORT_RDY_MASK0 + (port / 32) * 4);
	val &= ~BIT(port % 32);
	sys_write32(val, reg_base + MCB_REG_PORT_RDY_MASK0 + (port / 32) * 4);
}

int mcb_systech_rx_is_ready(const struct device *dev, uint8_t port)
{
	uint32_t reg_base = DEVICE_MMIO_NAMED_GET(dev, reg);
	uint32_t val, reg;

	if (port >= MCB_MAX_PORT) {
		LOG_ERR("Invalid port number");
		return -EINVAL;
	}

	reg = MCB_REG_PORT_RDY_MASK0 + (port / 32) * 4;
	val = sys_read32(reg_base + reg);
	if (val & BIT(port % 32)) {
		return 1;
	}
	return 0;
}

static const struct mcb_driver_api mcb_systech_api = {
	.reset = mcb_systech_reset,
	.poll_time = mcb_systech_poll_time,
	.config_port = mcb_systech_config_port,
	.get_status = mcb_systech_get_status,
	.clr_status = mcb_systech_clr_status,
	.get_rx_buf = mcb_systech_get_rx_buf,
	.get_tx_buf = mcb_systech_get_tx_buf,
	.get_rx_len = mcb_systech_get_rx_len,
	.get_tx_len = mcb_systech_get_tx_len,
	.set_tx_len = mcb_systech_set_tx_len,
	.rx_clr = mcb_systech_rx_clr,
	.rx_is_ready = mcb_systech_rx_is_ready,
	.tx = mcb_systech_tx,
};

static int mcb_systech_init(const struct device *dev)
{
	DEVICE_MMIO_NAMED_MAP(dev, reg, K_MEM_CACHE_NONE);
	/* FIXME: For tx_buf and rx_buf, enable cache would be better idea */
	DEVICE_MMIO_NAMED_MAP(dev, rx_buf, K_MEM_CACHE_NONE);
	DEVICE_MMIO_NAMED_MAP(dev, tx_buf, K_MEM_CACHE_NONE);

	return 0;
}

#define MCB_SYSTECH_DEVICE(n)                                                                      \
	static struct mcb_systech_data mcb_systech_data_##n;                                       \
	static const struct mcb_systech_config mcb_systech_config_##n = {                          \
		DEVICE_MMIO_NAMED_ROM_INIT(reg, DT_DRV_INST(n)),                                   \
		DEVICE_MMIO_NAMED_ROM_INIT(rx_buf, DT_DRV_INST(n)),                                \
		DEVICE_MMIO_NAMED_ROM_INIT(tx_buf, DT_DRV_INST(n)),                                \
		.max_bandwidth = DT_INST_PROP(n, bandwidth),                                       \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, &mcb_systech_init, NULL, &mcb_systech_data_##n,                   \
			      &mcb_systech_config_##n, POST_KERNEL, CONFIG_MCB_INIT_PRIORITY,      \
			      &mcb_systech_api);
DT_INST_FOREACH_STATUS_OKAY(MCB_SYSTECH_DEVICE)
