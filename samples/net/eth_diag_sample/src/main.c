/*
 * Copyright (c) 2025 SYSTech Co.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_core.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(eth_diag_sample, LOG_LEVEL_INF);

/* Define thread stack size */
#define STACK_SIZE 2048

/* Check interval (milliseconds) */
#define CHECK_INTERVAL 2000

/* Main thread stack */
K_THREAD_STACK_DEFINE(thread_stack, STACK_SIZE);
static struct k_thread thread_data;

/**
 * @brief Get physical link status
 *
 * @param iface Network interface pointer
 * @return true Link is connected
 * @return false Link is disconnected
 */
static bool get_phy_link_status(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	const struct ethernet_api *api = dev->api;
	struct ethernet_config config = {0};
	bool link_up = false;

	/* Check if driver supports getting configuration */
	if (api && api->get_config) {
		/* Get link status */
		if (api->get_config(dev, ETHERNET_CONFIG_TYPE_LINK, &config) == 0) {
			/* Any speed link connection indicates the link is up */
			link_up = config.l.link_10bt || config.l.link_100bt || config.l.link_1000bt;
		}
	}

	return link_up;
}

/**
 * @brief Check receive error count
 *
 * @param iface Network interface pointer
 * @return int Error count or -1 if unavailable
 */
static int check_rx_errors(struct net_if *iface)
{
#if defined(CONFIG_NET_STATISTICS_ETHERNET)
	const struct device *dev = net_if_get_device(iface);
	const struct ethernet_api *api = dev->api;

	if (api && api->get_stats) {
		struct net_stats_eth *stats = api->get_stats(dev);

		if (stats) {
			/* Get receive error count */
			return stats->error_details.rx_crc_errors;
		}
	}
#endif /* CONFIG_NET_STATISTICS_ETHERNET */

	return -1;
}

/**
 * @brief Print interface diagnostic information
 *
 * @param iface Network interface pointer
 * @param idx Interface index
 */
static void print_interface_status(struct net_if *iface, int idx)
{
	bool link_status = get_phy_link_status(iface);
	int rx_errors = check_rx_errors(iface);

	LOG_INF("Interface %d: %s | Error count: %d", idx,
		link_status ? "Connected" : "Disconnected", rx_errors);
}

/**
 * @brief Diagnostic thread function
 */
static void diagnostic_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* Wait for network interface initialization */
	k_sleep(K_SECONDS(1));

	LOG_INF("Ethernet diagnostic started...");
	LOG_INF("------------------------------------");

	while (1) {
		int if_count = 0;

		/* Sleep for specified interval before checking */
		k_sleep(K_MSEC(CHECK_INTERVAL));

		/* Iterate through all ethernet interfaces */
		STRUCT_SECTION_FOREACH(net_if, iface) {
			/* Print interface status */
			print_interface_status(iface, if_count++);
		}

		if (if_count > 0) {
			LOG_INF("------------------------------------");
		} else {
			LOG_INF("No ethernet interfaces found");
		}
	}
}

int main(void)
{
	LOG_INF("Ethernet Diagnostic Report Sample Started");
	LOG_INF("Checking link status and RX error count every %d ms\n\n", CHECK_INTERVAL);

	/* Create diagnostic thread */
	k_thread_create(&thread_data, thread_stack, STACK_SIZE, diagnostic_thread, NULL, NULL, NULL,
			K_LOWEST_APPLICATION_THREAD_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&thread_data, "eth_diag");

	return 0;
}
