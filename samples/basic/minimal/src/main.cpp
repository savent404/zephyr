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

template <int depth>
void record(uint32_t v, int32_t *max, int32_t *min, int32_t *avg)
{
	static uint32_t rec[depth];
	static int idx = 0;
	static bool init = false;

	rec[idx] = v;

	if (idx == depth - 1) {
		init = true;
	}

	if (init) {
		int64_t max_v = 0;
		int64_t min_v = 0xFFFFFFFF;
		int64_t sum = 0;

		for (int i = 0; i < depth; i++) {
			if (rec[i] > max_v) {
				max_v = rec[i];
			}
			if (rec[i] < min_v) {
				min_v = rec[i];
			}
			sum += rec[i];
		}

		int32_t avg_v = sum / depth;

		if (max) {
			*max = (int32_t)(max_v - avg_v);
		}
		if (min) {
			*min = (int32_t)(min_v - avg_v);
		}
		if (avg) {
			*avg = (int32_t)(avg_v);
		}
	}
	idx = (idx + 1) % depth;
}

float fn_val(uint32_t val, uint8_t r, bool bipolar, double gain=1e3)
{
	double val_to_vol[] = {
		2.5 / (0x800000 - 1),
		1.25 / (0x800000 - 1),
		0.625 / (0x800000 - 1),
		0.3125 / (0x800000 - 1),
		0.15625 / (0x800000 - 1),
		0.078125 / (0x800000 - 1),
		0.0390625 / (0x800000 - 1),
		0.01953125 / (0x800000 - 1),
	};
	double v;
	
	if (bipolar) {
		v = ((int64_t)val - 0x800000) * val_to_vol[r];
	} else {
		v = val * val_to_vol[r];
	}
	v *= gain;
	return (float)v;
}

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

	// adc7124_8::cha_range range = adc7124_8::cha_range::CHA_RANGE_2_5V;
	auto range = adc7124_8::cha_ctrl_param::CHA_RANGE_2_5V;
	uint8_t fs_reject[] = { 48, 40}; // 50Hz, 60Hz
	{
		using namespace adc7124_8;

		adc_ctrl_param adc_ctrl = {
			.clk_ref = adc_ctrl_param::ADC_CLK_REF_INT,
			.mode = adc_ctrl_param::ADC_MODE_CONTINUE,
			.internal_vol_ref = true,
			.pwr_mode = adc_ctrl_param::ADC_PWR_MODE_FULL
		};

		adc.adc_config(adc_ctrl);
		adc.diag_config(static_cast<uint32_t>(adc_diag::DIAG_MASK));
		cha_ctrl_param ctrl = {
			.range = range,
			.ref = cha_ctrl_param::CHA_REF_1,
			.AIN_BUF_P = false,
			.AIN_BUF_N = false,
			.REF_BUF_P = false,
			.REF_BUF_N = false,
			.burnout = cha_ctrl_param::CHA_BURNOUT_OFF,
			.bipolar = true
		};
		cha_filter_param filter = {
			.type = cha_filter_param::FILTER_TYPE_SINC3,
			.reject_50_60Hz = true,
			.post = cha_filter_param::post_filter_reserved,
			.single_cycle = false,
			.fs = fs_reject[0],
		};
		for (int i = 0; i < 8; i++) {
			if (i < 2) {
				adc.cha_config(i, true ? true : false, ctrl, filter, adc_pin_mux::ADC_PIN_MUX_DIFF_AUTO);
			}
			else {
				adc.cha_config(i, false, ctrl, filter, adc_pin_mux::ADC_PIN_MUX_DIFF_AUTO);
			}
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
		if (ch == 1) {
			int32_t max = 0, min = 0;
			record<128>(val, &max, &min, nullptr);
			/* Print data */
			printk("Channel %d: %06x(%08.4fmV)\t {%06d, %06d, %08.3fuV}\tdiag: %06X\tspin: %lld us\tconvert: %lld ms\n", ch,
				val, fn_val(val, range, true), max, min, fn_val(max-min, range, false, 1e6),
				diag, k_ticks_to_us_near64(tick), k_ticks_to_ms_near64(convert_duration));
		}

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
