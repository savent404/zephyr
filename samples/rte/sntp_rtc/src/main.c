/**
 * Copyright (c) 2025 SYSFly Co.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/net/sntp.h>
#include <arpa/inet.h>
#include <time.h>

LOG_MODULE_REGISTER(sntp_rtc_sample, LOG_LEVEL_INF);

#if defined(CONFIG_NET_CONFIG_SNTP_INIT_SERVER)
#define SERVER_ADDR CONFIG_NET_CONFIG_SNTP_INIT_SERVER
#else
#define SERVER_ADDR "192.168.10.1"
#endif

/* RTC device tree definitions */
#if DT_NODE_HAS_STATUS(DT_ALIAS(rtc), okay)
/* Use RTC alias if available */
#define RTC_DEV_NODE DT_ALIAS(rtc)
#elif DT_NODE_EXISTS(DT_NODELABEL(rtc0))
/* Fallback to RTC node label */
#define RTC_DEV_NODE DT_NODELABEL(rtc0)
#else
#error "No suitable RTC device found in device tree"
#endif

#define SNTP_PORT             (123)
#define TIME_UPDATE_INTERVAL  (10)       /* Time update interval in seconds */
#define CHINA_TIMEZONE_OFFSET (8 * 3600) /* China timezone offset (UTC+8) in seconds */

/* Convert epoch time to RTC time structure, considering timezone */
static void epoch_to_rtc_time(time_t epoch, struct rtc_time *rtc_tm, bool apply_timezone)
{
	struct tm *time_info;
	time_t adjusted_time = epoch;

	/* Apply timezone offset */
	if (apply_timezone) {
		adjusted_time += CHINA_TIMEZONE_OFFSET;
	}

	time_info = gmtime(&adjusted_time);
	if (time_info == NULL) {
		LOG_ERR("Failed to convert time");
		return;
	}

	rtc_tm->tm_sec = time_info->tm_sec;
	rtc_tm->tm_min = time_info->tm_min;
	rtc_tm->tm_hour = time_info->tm_hour;
	rtc_tm->tm_mday = time_info->tm_mday;
	rtc_tm->tm_mon = time_info->tm_mon;
	rtc_tm->tm_year = time_info->tm_year;
	rtc_tm->tm_wday = time_info->tm_wday;
	rtc_tm->tm_yday = time_info->tm_yday;
	rtc_tm->tm_isdst = time_info->tm_isdst;
}

/* Print RTC time */
static void print_rtc_time(const struct rtc_time *rtc_tm)
{
	LOG_INF("RTC Time: %04d-%02d-%02d %02d:%02d:%02d", rtc_tm->tm_year + 1900,
		rtc_tm->tm_mon + 1, rtc_tm->tm_mday, rtc_tm->tm_hour, rtc_tm->tm_min,
		rtc_tm->tm_sec);
}

/* Print network time */
static void print_network_time(const struct tm *time_info)
{
	LOG_INF("Network Time (UTC+8): %04d-%02d-%02d %02d:%02d:%02d", time_info->tm_year + 1900,
		time_info->tm_mon + 1, time_info->tm_mday, time_info->tm_hour, time_info->tm_min,
		time_info->tm_sec);
}

int main(void)
{
	const struct device *rtc_dev;
	struct sntp_ctx ctx;
	struct sockaddr_in addr;
	struct sntp_time sntp_timestamp;
	struct rtc_time rtc_tm;
	int rv;
	int retries = 0;

	/* Get RTC device from device tree */
	rtc_dev = DEVICE_DT_GET(RTC_DEV_NODE);
	if (!device_is_ready(rtc_dev)) {
		LOG_ERR("RTC device not ready");
		return -1;
	}
	LOG_INF("RTC device ready\n\n");

	/* Read and print current RTC time */
	rv = rtc_get_time(rtc_dev, &rtc_tm);
	if (rv < 0) {
		LOG_ERR("Failed to read initial RTC time: %d", rv);
	} else {
		LOG_INF("Initial RTC time:");
		print_rtc_time(&rtc_tm);
	}

	/* Allow network stack to initialize */
	LOG_INF("Waiting for network to initialize...");
	k_sleep(K_SECONDS(5));
	LOG_INF("Network initialization wait completed");

	/* Initialize SNTP client IPv4 configuration */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SNTP_PORT);
	inet_pton(AF_INET, SERVER_ADDR, &addr.sin_addr);
	LOG_INF("SNTP server address: %s\n", SERVER_ADDR);

	/* Initialize SNTP context */
	rv = sntp_init(&ctx, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
	if (rv < 0) {
		LOG_ERR("SNTP initialization failed: %d", rv);
		return -1;
	}

	while (1) {
		/* Send SNTP request */
		LOG_INF("Sending SNTP request...");
		rv = sntp_query(&ctx, 10 * MSEC_PER_SEC, &sntp_timestamp);
		if (rv < 0) {
			LOG_ERR("SNTP request failed: %d", rv);
			retries++;
			if (retries > 5) {
				LOG_ERR("SNTP request failed after 5 retries");
				break;
			}
			LOG_INF("Retrying in %d s...", TIME_UPDATE_INTERVAL);
			k_sleep(K_SECONDS(TIME_UPDATE_INTERVAL));
			continue;
		}

		LOG_INF("SNTP status: %d", rv);
		LOG_INF("Epoch time: %llu seconds", (unsigned long long)sntp_timestamp.seconds);

		/* Get current time (UTC) */
		time_t current_time = (time_t)sntp_timestamp.seconds;

		/* Apply timezone offset and print */
		time_t china_time = current_time + CHINA_TIMEZONE_OFFSET;
		struct tm *time_info = gmtime(&china_time);

		if (time_info == NULL) {
			LOG_ERR("Time conversion failed");
			k_sleep(K_SECONDS(TIME_UPDATE_INTERVAL));
			continue;
		}

		/* Print network time with timezone applied */
		print_network_time(time_info);

		/* Convert to RTC format and update RTC */
		epoch_to_rtc_time(current_time, &rtc_tm, true);
		rv = rtc_set_time(rtc_dev, &rtc_tm);
		if (rv < 0) {
			LOG_ERR("Failed to set RTC time: %d", rv);
		} else {
			LOG_INF("RTC time updated.");
		}

		/* Read and display RTC time to confirm successful update */
		rv = rtc_get_time(rtc_dev, &rtc_tm);
		if (rv < 0) {
			LOG_ERR("Failed to read RTC time: %d", rv);
		} else {
			print_rtc_time(&rtc_tm);
		}

		/* Wait for next update */
		LOG_INF("Waiting %d seconds for next update...\n\n", TIME_UPDATE_INTERVAL);
		k_sleep(K_SECONDS(TIME_UPDATE_INTERVAL));
	}

	/* Close SNTP context */
	sntp_close(&ctx);
	return 0;
}
