/*
 * Copyright (c) 2025 SYSFly Co.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT netforward_sf2507

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/syscon.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/gpio.h>

LOG_MODULE_REGISTER(sf2507, CONFIG_SYSCON_LOG_LEVEL);

struct sf2507_config {
	struct spi_dt_spec bus;
	const uint16_t *reg_init;
	size_t num_reg_pairs;
	struct gpio_dt_spec reset_gpio;
	uint32_t reset_delay;
	uint16_t slave_id;
};

struct sf2507_data {
	struct k_mutex lock;
};

/* SF2507 SPI operation codes */
#define SF2507_SPI_WRITE_OP 0x02
#define SF2507_SPI_READ_OP  0x03

static int sf2507_write_reg(const struct device *dev, uint16_t addr, uint16_t value)
{
	const struct sf2507_config *config = dev->config;
	struct sf2507_data *data = dev->data;
	uint8_t tx_buf[5];
	struct spi_buf tx = {
		.buf = tx_buf,
		.len = sizeof(tx_buf),
	};
	const struct spi_buf_set tx_bufs = {
		.buffers = &tx,
		.count = 1,
	};
	struct spi_config spi_cfg = {
		.frequency = config->bus.config.frequency,
		.operation = config->bus.config.operation,
		.slave = config->slave_id,
		.cs = config->bus.config.cs,
	};
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	tx_buf[0] = SF2507_SPI_WRITE_OP;
	tx_buf[1] = (addr >> 8) & 0xFF;
	tx_buf[2] = addr & 0xFF;
	tx_buf[3] = (value >> 8) & 0xFF;
	tx_buf[4] = value & 0xFF;

	ret = spi_write(config->bus.bus, &spi_cfg, &tx_bufs);
	if (ret < 0) {
		LOG_ERR("SPI write failed: %d", ret);
	}

	k_mutex_unlock(&data->lock);
	return ret;
}

static int sf2507_read_reg(const struct device *dev, uint16_t addr, uint16_t *value)
{
	const struct sf2507_config *config = dev->config;
	struct sf2507_data *data = dev->data;
	uint8_t tx_buf[5];
	uint8_t rx_buf[5];
	const struct spi_buf tx = {
		.buf = tx_buf,
		.len = sizeof(tx_buf),
	};
	const struct spi_buf rx = {
		.buf = rx_buf,
		.len = sizeof(rx_buf),
	};
	const struct spi_buf_set tx_bufs = {
		.buffers = &tx,
		.count = 1,
	};
	const struct spi_buf_set rx_bufs = {
		.buffers = &rx,
		.count = 1,
	};
	struct spi_config spi_cfg = {
		.frequency = config->bus.config.frequency,
		.operation = config->bus.config.operation,
		.slave = config->slave_id,
		.cs = config->bus.config.cs,
	};
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	tx_buf[0] = SF2507_SPI_READ_OP;
	tx_buf[1] = (addr >> 8) & 0xFF;
	tx_buf[2] = addr & 0xFF;

	ret = spi_transceive(config->bus.bus, &spi_cfg, &tx_bufs, &rx_bufs);
	if (ret < 0) {
		LOG_ERR("SPI read failed: %d", ret);
	} else {
		*value = sys_get_be16(&rx_buf[3]);
	}

	k_mutex_unlock(&data->lock);
	return ret;
}

static int sf2507_reg_read(const struct device *dev, uint16_t addr, uint32_t *value)
{
	uint16_t val16 = 0;
	int ret;

	ret = sf2507_read_reg(dev, addr, &val16);
	*value = (uint32_t)val16;

	return ret;
}

static int sf2507_reg_write(const struct device *dev, uint16_t addr, uint32_t value)
{
	return sf2507_write_reg(dev, addr, (uint16_t)value);
}

static const struct syscon_driver_api sf2507_driver_api = {
	.read = sf2507_reg_read,
	.write = sf2507_reg_write,
};

static void sf2507_reset(const struct device *dev)
{
	const struct sf2507_config *config = dev->config;
	static uint32_t reset_cnt_bitmap;

	if (config->reset_gpio.port) {
		if (!device_is_ready(config->reset_gpio.port)) {
			LOG_ERR("Reset GPIO port %s not ready", config->reset_gpio.port->name);
			return;
		}

		if (!gpio_is_ready_dt(&config->reset_gpio)) {
			LOG_ERR("Reset GPIO %s not ready", config->reset_gpio.port->name);
			return;
		}

		if (reset_cnt_bitmap & (1 << config->reset_gpio.pin)) {
			LOG_WRN("Reset GPIO %s already set", config->reset_gpio.port->name);
			return;
		}
		reset_cnt_bitmap |= 1 << config->reset_gpio.pin;

		gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT_ACTIVE);
		k_msleep(5);
		gpio_pin_set_dt(&config->reset_gpio, 1);
		k_msleep(config->reset_delay);
	}
}

static int sf2507_init(const struct device *dev)
{
	const struct sf2507_config *config = dev->config;
	struct sf2507_data *data = dev->data;
	int i, ret;

	sf2507_reset(dev);

	if (!spi_is_ready_dt(&config->bus)) {
		LOG_ERR("SPI bus %s not ready", config->bus.bus->name);
		return -ENODEV;
	}

	k_mutex_init(&data->lock);

	LOG_INF("Initializing SF2507 device %s", dev->name);

	/* Initialize registers from device tree configuration */
	for (i = 0; i < config->num_reg_pairs; i += 2) {
		uint16_t addr = config->reg_init[i];
		uint16_t val = config->reg_init[i + 1];

		ret = sf2507_write_reg(dev, addr, val);
		if (ret < 0) {
			LOG_ERR("Failed to initialize register 0x%04x", addr);
			return ret;
		}
	}
	k_msleep(10);

	LOG_INF("SF2507 device %s initialized", dev->name);

	return 0;
}
#define _SF2507_INIT(inst)                                                                         \
	static const uint16_t sf2507_reg_init_##inst[] = DT_INST_PROP(inst, register_init);        \
	static const struct sf2507_config sf2507_config_##inst = {                                 \
		.bus = SPI_DT_SPEC_INST_GET(                                                       \
			inst, SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0),         \
		.reg_init = sf2507_reg_init_##inst,                                                \
		.num_reg_pairs = ARRAY_SIZE(sf2507_reg_init_##inst),                               \
		.reset_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, gpios, {0}),                          \
		.reset_delay = DT_INST_PROP(inst, reset_delay),                                    \
		.slave_id = DT_INST_PROP(inst, slave_id),                                          \
	};                                                                                         \
	static struct sf2507_data sf2507_data_##inst;                                              \
	DEVICE_DT_INST_DEFINE(inst, sf2507_init, NULL, &sf2507_data_##inst, &sf2507_config_##inst, \
			      POST_KERNEL, 99, &sf2507_driver_api);

DT_INST_FOREACH_STATUS_OKAY(_SF2507_INIT)
