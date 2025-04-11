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

LOG_MODULE_REGISTER(mcb_systech, LOG_LEVEL);

#define DT_DRV_COMPAT systech_mcb

#define DEV_CFG(_dev)  ((const struct mcb_systech_config *const)(_dev)->config)
#define DEV_DATA(_dev) ((struct mcb_systech_data *const)(_dev)->data)

struct mcb_systech_data {
	DEVICE_MMIO_NAMED_RAM(reg);
	DEVICE_MMIO_NAMED_RAM(rx_buf);
	DEVICE_MMIO_NAMED_RAM(tx_buf);
	uint32_t bandwidth;
	uint32_t pool_time;
	uint8_t self_sid;
};

struct mcb_systech_config {
	DEVICE_MMIO_NAMED_ROM(reg);
	DEVICE_MMIO_NAMED_ROM(rx_buf);
	DEVICE_MMIO_NAMED_ROM(tx_buf);
	uint32_t pps;
	uint32_t poll_time;
	bool slow_mode;
};

static inline void mcb_busy_wait(uint32_t us)
{
	for (int i = 0; i < us * 100; i++) {
		__asm volatile("nop");
	}
}

static inline void mcb_write_unsafe(uint32_t value, uint32_t addr)
{
	sys_write32(value, addr);
	LOG_DBG("Write %08x to %08x", value, addr);
}

static inline uint32_t mcb_read(const struct device *dev, uint32_t addr)
{
	const struct mcb_systech_config *cfg = DEV_CFG(dev);
	uint32_t val;

	if (cfg->slow_mode) {
		mcb_busy_wait(1);
	}
	val = sys_read32(addr);
	LOG_DBG("Read %08x from %08x", val, addr);
	return val;
}

static inline void mcb_write(const struct device *dev, uint32_t value, uint32_t addr)
{
#if CONFIG_MCB_SYSTECH_HW_WORKAROUND
#define MCB_WRITE_MAX_RETRY 5
#else
#define MCB_WRITE_MAX_RETRY 1
#endif

	uint32_t read_retry = MCB_WRITE_MAX_RETRY;
	uint32_t val;

	mcb_write_unsafe(value, addr);

	do {
		val = mcb_read(dev, addr);
	} while (val != value && --read_retry);

	if (val != value) {
		LOG_WRN("Write %08x to %08x failed, read %08x", value, addr, val);
	} else if (read_retry + 1 < MCB_WRITE_MAX_RETRY) {
		LOG_WRN("Write %08x to %08x, read retry %d", value, addr,
			MCB_WRITE_MAX_RETRY - read_retry - 1);
	}
}

void mcb_systech_reset(const struct device *dev, uint8_t role)
{
	struct mcb_systech_data *data = dev->data;
	uint32_t reg_base = DEVICE_MMIO_NAMED_GET(dev, reg);
	uint32_t val;

	/* Clear CTRL/STATUS register */
	mcb_write(dev, 0, reg_base + MCB_REG_CTRL1);
	mcb_write(dev, 0, reg_base + MCB_REG_CTRL2);
	mcb_write(dev, 0, reg_base + MCB_REG_STATUS1);
	mcb_write(dev, 0, reg_base + MCB_REG_I_A_COUNT);
	mcb_write(dev, 0, reg_base + MCB_REG_I_B_COUNT);
	mcb_write(dev, 0, reg_base + MCB_REG_PORT_MASK0);
	mcb_write(dev, 0, reg_base + MCB_REG_PORT_RDY_MASK0);

	switch (role) {
	case _MCB_ROLE_MASTER:
		val = (DR_MPU_P << r_MCB_CTRL2_DR_pos) | (DT_MPU_P << r_MCB_CTRL2_DT_pos) |
		      (3000 << r_MCB_CTRL2_PT_pos);
		LOG_DBG("Set MCB to master mode, reg: %08x", val);
		break;
	case _MCB_ROLE_SLAVE:
	default:
		val = (DR_MPU_B << r_MCB_CTRL2_DR_pos) | (DT_MPU_B << r_MCB_CTRL2_DT_pos);
		LOG_DBG("Set MCB to slave mode, reg: %08x", val);
		break;
	}
	mcb_write(dev, val, reg_base + MCB_REG_CTRL2);

	/* Enable RxEN as default */
	mcb_write(dev, b_MCB_CTRL1_RxEN, reg_base + MCB_REG_CTRL1);

	struct mcb_info info = {};

	mcb_get_mcb_info(dev, &info);
	data->self_sid = (uint8_t)(info.slot_id);
	LOG_DBG("Reset MCB, role %d, sid %d", role, data->self_sid);
}

void mcb_systech_poll_time(const struct device *dev, uint32_t timeout)
{
	uint32_t reg_base = DEVICE_MMIO_NAMED_GET(dev, reg);
	struct mcb_systech_data *data = dev->data;
	uint32_t val;

	timeout = timeout / 10; /* Register POLL_TIME is in 10ns unit */
	data->pool_time = timeout;
	val = mcb_read(dev, reg_base + MCB_REG_CTRL2);
	val &= ~(r_MCB_CTRL2_PT_mask << r_MCB_CTRL2_PT_pos);
	val |= (timeout & r_MCB_CTRL2_PT_mask) << r_MCB_CTRL2_PT_pos;
	LOG_INF("Set poll time to %dns, reg: %x", timeout * 10, val);
	mcb_write(dev, val, reg_base + MCB_REG_CTRL2);
}

void mcb_systech_preempt(const struct device *dev, bool preempt)
{
	uint32_t reg_base = DEVICE_MMIO_NAMED_GET(dev, reg);
	uint32_t val;

	val = mcb_read(dev, reg_base + MCB_REG_CTRL1);
	val |= (preempt ? R_Ack_set : R_Ack_echo) << 2;
	LOG_INF("Set preempt to %d, reg: %08x", preempt, val);
	mcb_write(dev, val, reg_base + MCB_REG_CTRL1);
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

	/* setup none-critical parts */
	mcb_write(dev, max_rx_len, reg_base + MCB_REG_PORT_RX_MAX(port));

	/* clear port ready mask */
	val = mcb_read(dev, reg_base + MCB_REG_PORT_RDY_MASK0 + port_idx * 4);
	val &= ~port_bit;
	mcb_write(dev, val, reg_base + MCB_REG_PORT_RDY_MASK0 + port_idx * 4);

	/* reset write mask */
	val = mcb_read(dev, reg_base + MCB_REG_PORT_W_MASK0 + port_idx * 4);
	val &= ~port_bit;
	val |= write ? port_bit : 0;
	mcb_write(dev, val, reg_base + MCB_REG_PORT_W_MASK0 + port_idx * 4);

	/* enable/disable port */
	val = mcb_read(dev, reg_base + MCB_REG_PORT_MASK0 + port_idx * 4);
	if (enable) {
		val |= port_bit;
	} else {
		val &= ~port_bit;
	}
	mcb_write(dev, val, reg_base + MCB_REG_PORT_MASK0 + port_idx * 4);

	LOG_INF("port(%d) config: %s-%s-%d", port, enable ? "Y" : "N", write ? "W" : "R",
		max_rx_len);
}

void mcb_systech_tx(const struct device *dev, uint8_t sid, uint8_t port, bool preempt)
{
	struct mcb_systech_data *data = dev->data;
	uint32_t reg_base = DEVICE_MMIO_NAMED_GET(dev, reg);
	uint32_t val, wanted;

	if (port >= MCB_MAX_PORT) {
		LOG_ERR("Invalid port number");
		return;
	}

	if (mcb_get_tx_len(dev, port) % CONFIG_MCB_SYSTECH_BUFFER_ALIGN) {
		/* HW bug, can't send frame with length not multiple of 4 */
		LOG_WRN("Can't send frame with length not multiple of %d, sid %d, port %d",
			CONFIG_MCB_SYSTECH_BUFFER_ALIGN, sid, port);
		k_panic();
	}

	val = (sid & r_MCB_CTRL1_D_SID_mask) << r_MCB_CTRL1_D_SID_pos;
	val |= (data->self_sid & r_MCB_CTRL1_S_SID_mask) << r_MCB_CTRL1_S_SID_pos;
	val |= (port & r_MCB_CTRL1_PORT_mask) << r_MCB_CTRL1_PORT_pos;
	val |= b_MCB_CTRL1_TxEN | b_MCB_CTRL1_RxEN;
	val |= (preempt ? R_Ack_set : R_Ack_echo) << 2;
	mcb_write(dev, val, reg_base + MCB_REG_CTRL1);

	wanted = val;
	val = mcb_read(dev, reg_base + MCB_REG_CTRL1);
	if (val != wanted) {
		LOG_WRN("tx reg mismatch, reg: %08x, wanted: %08x", val, wanted);
	}
	LOG_DBG("Trigger transmission to sid %d, port %d, len %d", sid, port,
		mcb_get_tx_len(dev, port));
	if (mcb_get_tx_len(dev, port)) {
		LOG_HEXDUMP_DBG(mcb_get_tx_buf(dev, port), mcb_get_tx_len(dev, port), "BUF");
	}
}

void mcb_systech_get_status(const struct device *dev, uint32_t *s)
{
	uint32_t reg_base = DEVICE_MMIO_NAMED_GET(dev, reg);
	uint32_t val, status = 0, reg;

	reg = MCB_REG_STATUS1;
	val = mcb_read(dev, reg_base + reg);

	LOG_DBG("Read status register val = 0x%08x", val);

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
	uint32_t val = 0;

	if (bits == 0) {
		return;
	}

	if (bits & _MCB_ERR_R_ERR) {
		val |= b_MCB_STATUS1_RE;
	}
	if (bits & _MCB_ERR_T_ERR) {
		val |= b_MCB_STATUS1_TE;
	}
	if (bits & _MCB_ERR_I_ERR) {
		val |= b_MCB_STATUS1_IE;
	}
	if (bits & _MCB_ERR_PREEMPT) {
		val |= b_MCB_STATUS1_RF;
	}
	if (bits & _MCB_ERR_P_ERR) {
		val |= b_MCB_STATUS1_PE;
	}

	mcb_write_unsafe(~val, reg_base + MCB_REG_STATUS1);
	/* Make sure status register is cleared */
	if (mcb_read(dev, reg_base + MCB_REG_STATUS1) & val) {
		LOG_WRN_ONCE("Failed to clear status register, val = 0x%x, reg = 0x%x", val,
			     mcb_read(dev, reg_base + MCB_REG_STATUS1));
	}
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
	val = mcb_read(dev, reg_base + MCB_REG_PORT_RX_LEN(port));
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
	val = mcb_read(dev, reg_base + MCB_REG_PORT_TX_LEN(port));
	return val;
}

void mcb_systech_set_tx_len(const struct device *dev, uint8_t port, uint16_t len)
{
	uint32_t reg_base = DEVICE_MMIO_NAMED_GET(dev, reg);

	if (port >= MCB_MAX_PORT) {
		LOG_ERR("Invalid port number");
		return;
	}
	if (len % CONFIG_MCB_SYSTECH_BUFFER_ALIGN) {
		LOG_WRN("Invalid tx len %d, set to align %d", len, CONFIG_MCB_SYSTECH_BUFFER_ALIGN);
		len = (len + CONFIG_MCB_SYSTECH_BUFFER_ALIGN) & ~CONFIG_MCB_SYSTECH_BUFFER_ALIGN;
	}
	if (len > CONFIG_MCB_SYSTECH_BUFFER_MAX_LENGTH) {
		LOG_WRN("Invalid tx len %d, set to max %d", len,
			CONFIG_MCB_SYSTECH_BUFFER_MAX_LENGTH);
		len = CONFIG_MCB_SYSTECH_BUFFER_MAX_LENGTH;
	}

	if (mcb_systech_get_tx_len(dev, port) == len) {
		return;
	}
	mcb_write(dev, len, reg_base + MCB_REG_PORT_TX_LEN(port));

#if CONFIG_MCB_SYSTECH_HW_WORKAROUND
	uint32_t retry = 10;

	while (mcb_systech_get_tx_len(dev, port) != len && --retry) {
		LOG_WRN("MCB: TX len not set correctly, retry %d...", retry);
		if (--retry) {
			mcb_write(dev, len, reg_base + MCB_REG_PORT_TX_LEN(port));
			mcb_busy_wait(1);
		} else {
			LOG_ERR("set_tx_len error can't be recovery, panic!");
			k_panic();
			break;
		}
	}
#endif
}

void mcb_systech_rx_clr(const struct device *dev, uint8_t port)
{
	uint32_t reg_base = DEVICE_MMIO_NAMED_GET(dev, reg);
	uint32_t val, reg;

	/* As the specific of MCB, clear status flag is to write 0 to the bit */
	val = b_MCB_STATUS1_RDY;
	mcb_write_unsafe(~val, reg_base + MCB_REG_STATUS1);
	if (mcb_read(dev, reg_base + MCB_REG_STATUS1) & val) {
		LOG_WRN_ONCE("Failed to clear rx ready, val = 0x%x, reg = 0x%x", val,
			     mcb_read(dev, reg_base + MCB_REG_STATUS1));
	}

	if (port >= MCB_MAX_PORT) {
		LOG_ERR("Invalid port number");
		return;
	}
	val = BIT(port % 32);
	reg = MCB_REG_PORT_RDY_MASK0 + (port / 32) * 4;
	mcb_write_unsafe(~val, reg_base + reg);
	if (mcb_read(dev, reg_base + reg) & val) {
		LOG_WRN_ONCE("Failed to clear port ready mask, val = 0x%x, reg = 0x%x", val,
			     mcb_read(dev, reg_base + reg));
	}
}

int mcb_systech_rx_is_ready(const struct device *dev, uint8_t port)
{
	uint32_t reg_base = DEVICE_MMIO_NAMED_GET(dev, reg);
	uint32_t val, reg;

	if (port >= MCB_MAX_PORT) {
		LOG_ERR("Invalid port number");
		return -EINVAL;
	}

	/* FIXME: check status1.rdy bit also. */
	reg = MCB_REG_PORT_RDY_MASK0 + (port / 32) * 4;
	val = mcb_read(dev, reg_base + reg);
	if (val & BIT((port % 32))) {
		LOG_INF("Port %d is ready, mask=%08x", port, val);
		if (mcb_get_rx_len(dev, port)) {
			LOG_HEXDUMP_INF(mcb_get_rx_buf(dev, port), mcb_get_rx_len(dev, port),
					"BUF");
		} else {
			LOG_WRN("Port %d is ready, but no data", port);
		}
		return 1;
	}

	return 0;
}

static void mcb_systech_get_mcb_info(const struct device *dev, struct mcb_info *info)
{
	const struct mcb_systech_config *cfg = DEV_CFG(dev);
	uint32_t reg_base = DEVICE_MMIO_NAMED_GET(dev, reg);
	uint32_t dev_info[3];

	memset(info, 0, sizeof(struct mcb_info));

	dev_info[0] = mcb_read(dev, reg_base + MCB_REG_DEVICE_INF0);
	dev_info[1] = mcb_read(dev, reg_base + MCB_REG_DEVICE_INF1);
	dev_info[2] = mcb_read(dev, reg_base + MCB_REG_DEVICE_INF2);

	info->slot_id = (dev_info[0] & b_MCB_SLOT_ID_MASK) >> b_MCB_SLOT_ID_POS;
	info->i_err[0] = mcb_read(dev, reg_base + MCB_REG_I_A_COUNT);
	info->i_err[1] = mcb_read(dev, reg_base + MCB_REG_I_B_COUNT);
	info->hw_version = (dev_info[1]);
	info->packet_per_second = cfg->pps;
	info->poll_time = cfg->poll_time;
}

void mcb_systech_set_role(const struct device *dev, uint8_t dr, uint8_t dt)
{
	uint32_t reg_base = DEVICE_MMIO_NAMED_GET(dev, reg);
	uint32_t val;
	uint32_t *rx_buf;
	uint8_t msg[8];

	val = mcb_read(dev, reg_base + MCB_REG_CTRL2);
	val &= ~(r_MCB_CTRL2_DR_mask << r_MCB_CTRL2_DR_pos);
	val &= ~(r_MCB_CTRL2_DT_mask << r_MCB_CTRL2_DT_pos);
	val |= (dr & r_MCB_CTRL2_DR_mask) << r_MCB_CTRL2_DR_pos;
	val |= (dt & r_MCB_CTRL2_DT_mask) << r_MCB_CTRL2_DT_pos;
	LOG_INF("Set DR/DT to %d/%d, reg: %08x", dr, dt, val);
	mcb_write(dev, val, reg_base + MCB_REG_CTRL2);

	LOG_DBG("Auto update discovery port...");
	rx_buf = mcb_systech_get_tx_buf(dev, 0);
	val = *rx_buf; /* DR[7:0], DT[15:8] */
	val &= 0xFFFF0000;
	val |= (dr & 0xFF) | ((dt & 0xFF) << 8);
	*rx_buf = val;
	*(uint32_t *)msg = rx_buf[0];
	*(uint32_t *)(msg + 4) = rx_buf[1];
	LOG_HEXDUMP_INF(msg, 8, "Update discovery port buffer");
	mcb_systech_set_tx_len(dev, 0, 8);
	mcb_systech_config_port(dev, 0, true, false, 0);
	LOG_DBG("Auto update discovery port...DONE");
}

static const struct mcb_driver_api mcb_systech_api = {
	.reset = mcb_systech_reset,
	.poll_time = mcb_systech_poll_time,
	.set_role = mcb_systech_set_role,
	.preempt = mcb_systech_preempt,
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
	.get_mcb_info = mcb_systech_get_mcb_info,
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
		.pps = DT_INST_PROP(n, pps),                                                       \
		.poll_time = DT_INST_PROP(n, poll),                                                \
		.slow_mode = DT_INST_PROP(n, slow),                                                \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, &mcb_systech_init, NULL, &mcb_systech_data_##n,                   \
			      &mcb_systech_config_##n, POST_KERNEL, CONFIG_MCB_INIT_PRIORITY,      \
			      &mcb_systech_api);
DT_INST_FOREACH_STATUS_OKAY(MCB_SYSTECH_DEVICE)
