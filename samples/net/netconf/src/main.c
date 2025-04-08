/**
 * Copyright (c) 2025 SYSFly Co.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief IP & MAC Address Management Demo Application
 *
 * This application demonstrates how to:
 * 1. Get the current IP/MAC address from a network interface
 * 2. Modify the IP/MAC address on a network interface
 * 3. Package the functionality in reusable functions
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/ethernet_mgmt.h>

LOG_MODULE_REGISTER(ip_mac_test, LOG_LEVEL_DBG);

/**
 * @brief Convert MAC address to string
 *
 * @param mac MAC address bytes
 * @param str Output string buffer
 * @param len Length of buffer
 * @return int Length of formatted string
 */
static int mac_addr_to_str(const uint8_t *mac, char *str, int len)
{
	return snprintf(str, len, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3],
			mac[4], mac[5]);
}

/**
 * @brief Get IP address as string
 *
 * @param addr IPv4 address
 * @param buf Output buffer
 * @param len Buffer length
 * @return char* Pointer to buffer
 */
static char *ipv4_to_str(const struct in_addr *addr, char *buf, int len)
{
	return net_addr_ntop(AF_INET, addr, buf, len);
}

/**
 * @brief Get the current MAC address of a network interface
 *
 * @param iface Network interface
 * @param mac Output buffer for MAC address (must be at least 6 bytes)
 * @return int 0 on success, negative error code otherwise
 */
int get_mac_address(struct net_if *iface, uint8_t *mac)
{
	if (iface == NULL || mac == NULL) {
		return -EINVAL;
	}

	const struct net_linkaddr *linkaddr = net_if_get_link_addr(iface);

	if (linkaddr == NULL || linkaddr->addr == NULL || linkaddr->len < 6) {
		return -ENOENT;
	}

	memcpy(mac, linkaddr->addr, 6);
	return 0;
}

/**
 * @brief Set a new MAC address on a network interface
 *
 * @param iface Network interface
 * @param mac New MAC address (6 bytes)
 * @return int 0 on success, negative error code otherwise
 */
int set_mac_address(struct net_if *iface, const uint8_t *mac)
{
	if (iface == NULL || mac == NULL) {
		return -EINVAL;
	}

	/* Check if it's an Ethernet interface */
	if (net_if_l2(iface) != &NET_L2_GET_NAME(ETHERNET)) {
		LOG_ERR("MAC address can be set only for Ethernet interfaces");
		return -ENOTSUP;
	}

	struct ethernet_req_params params = {0};

	/* Copy MAC address to request parameters */
	memcpy(params.mac_address.addr, mac, sizeof(params.mac_address.addr));

	LOG_DBG("Setting MAC address to %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2],
		mac[3], mac[4], mac[5]);

	/* Call network management interface to set MAC address */
	int ret = net_mgmt(NET_REQUEST_ETHERNET_SET_MAC_ADDRESS, iface, &params, sizeof(params));

	if (ret < 0) {
		if (ret == -EACCES) {
			LOG_ERR("MAC address cannot be set when interface is operational");
		} else {
			LOG_ERR("Failed to set MAC address through driver (%d)", ret);
		}

		LOG_WRN("Falling back to link layer address update only");
		return ret;
	}

	LOG_INF("MAC address successfully set through driver");

	return 0;
}

/**
 * @brief Get IPv4 address of a network interface
 *
 * @param iface Network interface
 * @param addr Output IPv4 address
 * @return int 0 on success, negative error code otherwise
 */
int get_ipv4_address(struct net_if *iface, struct in_addr *addr)
{
	if (iface == NULL || addr == NULL) {
		return -EINVAL;
	}

	struct net_if_ipv4 *ipv4 = NULL;

	if (net_if_config_ipv4_get(iface, &ipv4) < 0 || ipv4 == NULL) {
		return -ENOENT;
	}

	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		if (ipv4->unicast[i].ipv4.is_used) {
			memcpy(addr, &ipv4->unicast[i].ipv4.address.in_addr,
			       sizeof(struct in_addr));
			return 0;
		}
	}

	return -ENOENT;
}

/**
 * @brief Remove all IPv4 addresses from a network interface
 *
 * @param iface Network interface
 * @return int 0 on success, negative error code otherwise
 */
int remove_all_ipv4_addresses(struct net_if *iface)
{
	if (iface == NULL) {
		return -EINVAL;
	}

	struct net_if_ipv4 *ipv4 = NULL;
	struct in_addr addr;
	int count = 0;

	if (net_if_config_ipv4_get(iface, &ipv4) < 0 || ipv4 == NULL) {
		return -ENOENT;
	}

	/* Iterate and remove all IPv4 addresses */
	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		if (ipv4->unicast[i].ipv4.is_used) {
			memcpy(&addr, &ipv4->unicast[i].ipv4.address.in_addr,
			       sizeof(struct in_addr));
			LOG_INF("Removing IP: %d.%d.%d.%d", ((uint8_t *)&addr)[0],
				((uint8_t *)&addr)[1], ((uint8_t *)&addr)[2],
				((uint8_t *)&addr)[3]);

			if (net_if_ipv4_addr_rm(iface, &addr)) {
				count++;
			}
			/* Reset counter, because deletion changes the array */
			i = -1;
		}
	}

	LOG_INF("Removed %d IPv4 addresses", count);
	return 0;
}

/**
 * @brief Set a new IPv4 address on a network interface
 *
 * @param iface Network interface
 * @param addr New IPv4 address
 * @param clear_existing Whether to remove existing addresses (true) or add alongside (false)
 * @return int 0 on success, negative error code otherwise
 */
int set_ipv4_address(struct net_if *iface, const struct in_addr *addr, bool clear_existing)
{
	if (iface == NULL || addr == NULL) {
		return -EINVAL;
	}

	struct net_if_addr *ifaddr;

	/* If needed, first clear existing IP addresses */
	if (clear_existing) {
		remove_all_ipv4_addresses(iface);
	}

	/* Add new IP address */
	ifaddr = net_if_ipv4_addr_add(iface, (struct in_addr *)addr, NET_ADDR_MANUAL, 0);
	if (ifaddr == NULL) {
		return -ENOMEM;
	}

	return 0;
}

/**
 * @brief Remove an IPv4 address from a network interface
 *
 * @param iface Network interface
 * @param addr IPv4 address to remove
 * @return int 0 on success, negative error code otherwise
 */
int remove_ipv4_address(struct net_if *iface, const struct in_addr *addr)
{
	if (iface == NULL || addr == NULL) {
		return -EINVAL;
	}

	if (!net_if_ipv4_addr_rm(iface, addr)) {
		return -ENOENT;
	}

	return 0;
}

/**
 * @brief Increments an IPv4 address by the specified value
 *
 * This function adds a value to the last octet of an IPv4 address
 *
 * @param original Original IPv4 address
 * @param increment Value to add to the last octet
 * @param result Output modified IPv4 address
 */
void increment_ipv4_address(const struct in_addr *original, uint8_t increment,
			    struct in_addr *result)
{
	if (original == NULL || result == NULL) {
		return;
	}

	uint8_t *addr_bytes = (uint8_t *)&original->s_addr;
	uint8_t *result_bytes = (uint8_t *)&result->s_addr;

	result_bytes[0] = addr_bytes[0];
	result_bytes[1] = addr_bytes[1];
	result_bytes[2] = addr_bytes[2];
	result_bytes[3] = addr_bytes[3] + increment;
}

/**
 * @brief Increments a MAC address by the specified value
 *
 * This function adds a value to the last octet of a MAC address
 *
 * @param original Original MAC address (6 bytes)
 * @param increment Value to add to the last octet
 * @param result Output modified MAC address (6 bytes)
 */
void increment_mac_address(const uint8_t *original, uint8_t increment, uint8_t *result)
{
	if (original == NULL || result == NULL) {
		return;
	}

	memcpy(result, original, 5);
	result[5] = original[5] + increment;
}

int main(void)
{
	/* Wait for network interface initialization */
	k_sleep(K_SECONDS(2));

	LOG_INF("--------------------------------");
	LOG_INF("IP/MAC Address Management Demo");
	LOG_INF("===============================\n\n");

	struct net_if *iface = net_if_get_default();

	if (iface == NULL) {
		LOG_ERR("No network interface found!");
		return -1;
	}

	/* Original IP & MAC address */
	struct in_addr orig_ip = {0};
	uint8_t orig_mac[6] = {0};
	char ip_str[NET_IPV4_ADDR_LEN];
	char mac_str[18];

	LOG_INF("Reading original IP & MAC...");

	if (get_ipv4_address(iface, &orig_ip) == 0) {
		LOG_INF("Original IP address: %s", ipv4_to_str(&orig_ip, ip_str, sizeof(ip_str)));
	} else {
		LOG_WRN("Could not get original IP address");
	}

	if (get_mac_address(iface, orig_mac) == 0) {
		mac_addr_to_str(orig_mac, mac_str, sizeof(mac_str));
		LOG_INF("Original MAC address: %s\n", mac_str);
	} else {
		LOG_WRN("Could not get original MAC address");
	}

	/* Modify addresses */
	LOG_INF("Modifying IP & MAC addresses...");

	/* Create new incremented addresses */
	struct in_addr new_ip = {0};
	uint8_t new_mac[6] = {0};

	increment_ipv4_address(&orig_ip, 1, &new_ip);
	increment_mac_address(orig_mac, 1, new_mac);

	/* First take interface down to set MAC address */
	if (net_if_is_up(iface)) {
		LOG_INF("Taking interface down to set MAC address");
		net_if_down(iface);
	}

	/* Set new MAC address */
	if (set_mac_address(iface, new_mac) == 0) {
		mac_addr_to_str(new_mac, mac_str, sizeof(mac_str));
		LOG_INF("Set new MAC address: %s", mac_str);
	} else {
		LOG_ERR("Failed to set new MAC address");
	}

	/* Use updated function to set IP address, and clear existing addresses */
	if (set_ipv4_address(iface, &new_ip, true) == 0) {
		LOG_INF("Set new IP address: %s", ipv4_to_str(&new_ip, ip_str, sizeof(ip_str)));
	} else {
		LOG_ERR("Failed to set new IP address");
	}

	/* Bring interface back up */
	LOG_INF("Bringing interface back up\n");
	net_if_up(iface);

	/* Verify changes */
	LOG_INF("Verifying IP & MAC addresses...");

	struct in_addr current_ip = {0};
	uint8_t current_mac[6] = {0};

	if (get_ipv4_address(iface, &current_ip) == 0) {
		LOG_INF("Current IP address: %s", ipv4_to_str(&current_ip, ip_str, sizeof(ip_str)));
	} else {
		LOG_WRN("Could not get current IP address");
	}

	if (get_mac_address(iface, current_mac) == 0) {
		mac_addr_to_str(current_mac, mac_str, sizeof(mac_str));
		LOG_INF("Current MAC address: %s\n", mac_str);
	} else {
		LOG_WRN("Could not get current MAC address");
	}

	LOG_INF("Demo complete. Network interfaces modified.");
	LOG_INF("Use 'net iface' shell command to verify changes.");

	LOG_INF("ping %s for testing...", ipv4_to_str(&new_ip, ip_str, sizeof(ip_str)));
	while (1) {
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
