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

	bool xfer(uint8_t *tx, uint8_t *rx, uint8_t tx_size, uint8_t rx_size) override;

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

	k_msleep(100);

	if (adc.is_alive()) {
		printk("ADC7124 detected\n");
	} else {
		printk("ADC7124 not detected\n");
	}

	adc.initialize();
	{
		using namespace adc7124_8;

		adc.adc_config(adc_clock_ref::ADC_CLK_REF_INT, adc_mode::ADC_MODE_CONTINUE, true, adc_pwr_mode::ADC_PWR_MODE_FULL);
		adc.diag_config(static_cast<uint32_t>(adc_diag::DIAG_MASK));
		cha_filter_param param = {
			.type = filter_type::FILTER_TYPE_SINC3,
			.reject_50_60Hz = true,
			.post = post_filter::post_filter_47hz,
			.single_cycle = false,
			.fs = 24,
		};
		for (int i = 0; i < 8; i++) {
			adc.cha_config(i, true ? true : false, param, adc_pin_mux::ADC_PIN_MUX_DIFF_AUTO);
		}
	}
	printk("ADC7124 initialized\n");

	int64_t convert_time = 0;
	int64_t convert_duration;

	while (1) {
		uint8_t status;
		uint32_t diag;
		uint8_t ch;
		uint32_t val;

		/* Check status */
		if (!adc.get_status(&status)) {
			printk("Error reading status\n");
			continue;
		}
		if (!adc.status_is_data_ready(status)) {
			continue;
		}
		ch = adc.status_get_curr_cha(status);
		convert_duration = k_uptime_ticks() - convert_time;

		/* check diagnostics */
		if (!adc.get_diag(&diag)) {
			printk("Error reading diag\n");
			continue;
		}

		/* Read data */
		if (adc.get_data(&val) != true) {
			printk("Error reading data: status %02X\n", status);
			continue;
		}

		/* spin till adc start to convert */
		int64_t tick = k_uptime_ticks();
		do {
			if (!adc.get_status(&status)) {
				printk("Error reading status\n");
				break;
			}
		} while (adc.status_is_data_ready(status));
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
