/*
 * Copyright (c) 2025 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>
#define LOG_MODULE_NAME main
LOG_MODULE_REGISTER(LOG_MODULE_NAME, LOG_LEVEL_DBG);

#define DELAY            2000000
#define ALARM_CHANNEL_ID 0

#if defined(CONFIG_BOARD_SAMD20_XPRO)
#define TIMER DT_NODELABEL(tc4)
#elif defined(CONFIG_SOC_FAMILY_ATMEL_SAM)
#define TIMER DT_NODELABEL(tc0)
#elif defined(CONFIG_COUNTER_MICROCHIP_MCP7940N)
#define TIMER DT_NODELABEL(extrtc0)
#elif defined(CONFIG_COUNTER_NRF_RTC)
#define TIMER DT_NODELABEL(rtc0)
#elif defined(CONFIG_COUNTER_TIMER_STM32)
#define TIMER DT_INST(0, st_stm32_counter)
#elif defined(CONFIG_COUNTER_RTC_STM32)
#define TIMER DT_INST(0, st_stm32_rtc)
#elif defined(CONFIG_COUNTER_SMARTBOND_TIMER)
#define TIMER DT_NODELABEL(timer3)
#elif defined(CONFIG_COUNTER_NATIVE_POSIX)
#define TIMER DT_NODELABEL(counter0)
#elif defined(CONFIG_COUNTER_XLNX_AXI_TIMER)
#define TIMER DT_INST(0, xlnx_xps_timer_1_00_a)
#elif defined(CONFIG_COUNTER_TMR_ESP32)
#define TIMER DT_NODELABEL(timer0)
#elif defined(CONFIG_COUNTER_MCUX_CTIMER)
#define TIMER DT_NODELABEL(ctimer0)
#elif defined(CONFIG_COUNTER_NXP_S32_SYS_TIMER)
#define TIMER DT_NODELABEL(stm0)
#elif defined(CONFIG_COUNTER_TIMER_GD32)
#define TIMER DT_NODELABEL(timer0)
#elif defined(CONFIG_COUNTER_GECKO_RTCC)
#define TIMER DT_NODELABEL(rtcc0)
#elif defined(CONFIG_COUNTER_GECKO_STIMER)
#define TIMER DT_NODELABEL(stimer0)
#elif defined(CONFIG_COUNTER_INFINEON_CAT1)
#define TIMER DT_NODELABEL(counter0_0)
#elif defined(CONFIG_COUNTER_AMBIQ)
#define TIMER DT_NODELABEL(counter0)
#elif defined(CONFIG_COUNTER_SNPS_DW)
#define TIMER DT_NODELABEL(timer0)
#elif defined(CONFIG_COUNTER_TIMER_RPI_PICO)
#define TIMER DT_NODELABEL(timer)
#elif defined(CONFIG_COUNTER_TIMER_MAX32)
#define TIMER DT_NODELABEL(counter0)
#else
#error Unable to find a counter device node in devicetree
#endif

#define NUM_ALARMS 3

struct counter_alarm_cfg alarm_cfg[NUM_ALARMS];

/*
 * Alarm callback: Print the channel to which the alarm belongs and the current time, and
 * then reset the alarm
 */
static void test_counter_interrupt_fn(const struct device *counter_dev, uint8_t chan_id,
				      uint32_t ticks, void *user_data)
{
	struct counter_alarm_cfg *cfg = user_data;
	uint32_t now_ticks;
	uint64_t alarm_usec;
	int alram_sec;
	int err;
	static uint32_t base_sec;
	static bool initialized;

	err = counter_get_value(counter_dev, &now_ticks);
	if (err) {
		printk("Failed to read counter value (err %d)\n", err);
		return;
	}

	alarm_usec = counter_ticks_to_us(counter_dev, now_ticks);
	alram_sec = alarm_usec / USEC_PER_SEC;

	printk("======Alarm on channel %d, NOW_sec: %u s ========\n", chan_id,
	       k_uptime_get() / 1000);

	if (cfg->ticks != 0U) {
		err = counter_set_channel_alarm(counter_dev, chan_id, cfg);
		if (err != 0) {
			printk("Error re-setting alarm on channel %d (err %d)\n", chan_id, err);
		}
	}
}

void main(void)
{
	const struct device *const counter_dev = DEVICE_DT_GET(TIMER);

	int err;

	printk("====Counter alarm sample - testing %d channels====\n\n", NUM_ALARMS);

	if (!device_is_ready(counter_dev)) {
		printk("Counter device not ready.\n");
		return;
	}

	err = counter_start(counter_dev);
	if (err) {
		printk("Failed to start counter (err %d)\n", err);
		return;
	}

	/* Set alarms for each channel separately */
	for (int i = 0; i < NUM_ALARMS; i++) {
		alarm_cfg[i].flags = 0;
		alarm_cfg[i].ticks = counter_us_to_ticks(counter_dev, DELAY * (i + 1));
		alarm_cfg[i].callback = test_counter_interrupt_fn;
		alarm_cfg[i].user_data = &alarm_cfg[i];

		err = counter_set_channel_alarm(counter_dev, i, &alarm_cfg[i]);
		if (err == 0) {
			printk("Set alarm on channel %d in %u sec (%u ticks)\n", i,
			       (uint32_t)(counter_ticks_to_us(counter_dev, alarm_cfg[i].ticks) /
					  USEC_PER_SEC),
			       alarm_cfg[i].ticks);
		} else if (err == -EINVAL) {
			printk("Alarm settings invalid for channel %d\n", i);
		} else if (err == -ENOTSUP) {
			printk("Alarm setting request not supported for channel %d\n", i);
		} else {
			printk("Error setting alarm on channel %d (err %d)\n", i, err);
		}
	}

	while (1) {
		k_sleep(K_FOREVER);
	}
}
