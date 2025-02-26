/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>

#include "ad7124-8.hpp"

#define SPI_ID DT_NODELABEL(spi1)
#define SPI_DEV DEVICE_DT_GET(SPI_ID)

struct spi_iface_zephyr: public spi_iface {
	explicit spi_iface_zephyr(const struct device *iface, struct spi_config *cfg, const struct gpio_dt_spec *cs_gpios) :
		iface_(iface),
		cfg_(cfg),
		cs_(cs_gpios)
	{
		gpio_pin_configure(iface_, cs_gpios->pin, GPIO_OUTPUT_INACTIVE);
	}
	virtual ~spi_iface_zephyr() {}

	bool xfer(uint8_t *tx, uint8_t *rx, uint8_t tx_size, uint8_t rx_size);
	uint8_t read8(uint8_t cmd) override;
	uint16_t read16(uint8_t cmd) override;
	uint32_t read24(uint8_t cmd) override;
	uint32_t read32(uint8_t cmd) override;
	void write8(uint8_t cmd, uint8_t data) override;
	void write16(uint8_t cmd, uint16_t data) override;
	void write24(uint8_t cmd, uint32_t data) override;
	void write32(uint8_t cmd, uint32_t data) override;

	const struct device*iface_;
	struct spi_config *cfg_;
	const struct gpio_dt_spec *cs_;
};


int main(void)
{
	const struct device *dev_spi = SPI_DEV;
	struct spi_config config;
	struct spi_cs_control cs_ctrl = (struct spi_cs_control){
		.gpio = GPIO_DT_SPEC_GET(SPI_ID, cs_gpios),
		.delay = 0u,
	};

	config.frequency = 1000000;
	config.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_MODE_CPOL | SPI_MODE_CPHA;
	config.slave = 0;
	config.cs = cs_ctrl;

	spi_iface_zephyr spi(dev_spi, &config, &cs_ctrl.gpio);

	adc7124_8::adc adc(&spi);

	if (adc.check_id()) {
		printk("ADC7124 detected\n");
	} else {
		printk("ADC7124 not detected\n");
	}

	{
		using namespace adc7124_8;

		/* FIXME: data_with_status is not working */
		adc.adc_config(adc_clock_ref::ADC_CLK_REF_INT, adc_mode::ADC_MODE_ONESHOT, true, false);

		/* FIXME: enable diag will block channel convert, need to handle diag firstly, then we can enable diag check */
#if 0
		adc.diag_config(-1);
#else
		adc.diag_config(0);
#endif
		cha_filter_param param = {
			.type = filter_type::FILTER_TYPE_SINC4,
			.reject_50_60Hz = true,
			.post = post_filter::post_filter_47hz,
			.single_cycle = false,
			.fs = 256,
		};
		for (int i = 0; i < 8; i++) {
			adc.cha_config(i, true, param, adc_pin_mux::ADC_PIN_MUX_DIFF_AUTO);
		}
	}

	int64_t convert_time = 0;
	int64_t convert_duration;

	while (1) {
		uint8_t status;
		uint32_t diag;
		uint8_t ch;
		uint32_t val;

		/* Check status */
		status = adc.read_status();
		if (!adc.status_is_data_ready(status)) {
			continue;
		}
		ch = adc.status_get_curr_cha(status);
		convert_duration = k_uptime_ticks() - convert_time;

		/* check diagnostics */
		diag = adc.read_diag();

		/* Read data */
		if (adc.read_data(&val, &status) != true) {
			printk("Error reading data: status %02X\n", status);
			continue;
		}

		/* spin till adc start to convert */
		int64_t tick = k_uptime_ticks();
		while (adc.status_is_data_ready(adc.read_status())) {
			k_busy_wait(1);
		}
		convert_time = k_uptime_ticks();
		tick = k_uptime_ticks() - tick;
		/* Print data */
		printk("Channel %d: %d\tdiag: %06X\tspin: %lld us\tconvert: %lld ms\n", ch,
			val, diag, k_ticks_to_us_near64(tick), k_ticks_to_ms_near64(convert_duration));

	}

	return 0;
}

bool spi_iface_zephyr::xfer(uint8_t *tx, uint8_t *rx, uint8_t tx_size, uint8_t rx_size)
{
	struct spi_buf tx_buf = {
		.buf = tx,
		.len = tx_size,
	};

	struct spi_buf rx_buf = {
		.buf = rx,
		.len = rx_size,
	};

	struct spi_buf_set tx_bufs = {
		.buffers = &tx_buf,
		.count = 1,
	};

	struct spi_buf_set rx_bufs = {
		.buffers = &rx_buf,
		.count = 1,
	};

	int ret = spi_transceive(iface_, cfg_, &tx_bufs, &rx_bufs);

	return !ret ? true : false;
}

void spi_iface_zephyr::write8(uint8_t cmd, uint8_t data)
{
	uint8_t tx[] = { cmd, data };
	xfer(tx, nullptr, sizeof(tx), 0);
}

void spi_iface_zephyr::write16(uint8_t cmd, uint16_t data)
{
	uint8_t tx[] = { cmd, (data >> 8) & 0xFF, data & 0xFF };
	xfer(tx, nullptr, sizeof(tx), 0);
}

void spi_iface_zephyr::write24(uint8_t cmd, uint32_t data)
{
	uint8_t tx[] = { cmd, (data >> 16) & 0xFF, (data >> 8) & 0xFF, data & 0xFF };
	xfer(tx, nullptr, sizeof(tx), 0);
}

void spi_iface_zephyr::write32(uint8_t cmd, uint32_t data)
{
	uint8_t tx[] = { cmd, (data >> 24) & 0xFF, (data >> 16) & 0xFF, (data >> 8) & 0xFF, data & 0xFF };
	xfer(tx, nullptr, sizeof(tx), 0);
}

uint8_t spi_iface_zephyr::read8(uint8_t cmd)
{
	uint8_t tx[] = { cmd, 0 };
	uint8_t rx[2];
	xfer(tx, rx, sizeof(tx), sizeof(rx));
	return rx[1];
}

uint16_t spi_iface_zephyr::read16(uint8_t cmd)
{
	uint8_t tx[] = { cmd, 0, 0 };
	uint8_t rx[3];
	xfer(tx, rx, sizeof(tx), sizeof(rx));
	return (rx[1] << 8) | rx[2];
}

uint32_t spi_iface_zephyr::read24(uint8_t cmd)
{
	uint8_t tx[] = { cmd, 0, 0, 0 };
	uint8_t rx[4];
	xfer(tx, rx, sizeof(tx), sizeof(rx));
	return (rx[1] << 16) | (rx[2] << 8) | rx[3];
}
uint32_t spi_iface_zephyr::read32(uint8_t cmd)
{
	uint8_t tx[] = { cmd, 0, 0, 0, 0 };
	uint8_t rx[5];
	xfer(tx, rx, sizeof(tx), sizeof(rx));
	return (rx[1] << 24) | (rx[2] << 16) | (rx[3] << 8) | rx[4];
}
