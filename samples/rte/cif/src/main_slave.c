/**
 * @license This project is CLOSED SOURCE, All Rights Reserved
 * Copyright (c) 2025 SYSTech Co.
 */
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/socketcif.h>
#include <zephyr/logging/log.h>

#include "cif_main.h"

LOG_MODULE_REGISTER(main_s, CONFIG_CIF_LOG_LEVEL);

struct reg_buf {
	uint8_t *reg;    /* Actual register buffer */
	uint8_t *modify; /* User modified buffer */
	uint16_t len;    /* Register length */
};

#if CONFIG_MCB_SYSTECH_HW_WORKAROUND
REG_BUF_REG_DEF(disc, 32) = {'d', 'i', 's', 'c', ':', '0', '0', '0'};
REG_BUF_DEF(disc, 32);
#endif
REG_BUF_REG_DEF(config, 32) = {'c', 'f', 'g', ':', '0', '0', '0', '0'};
REG_BUF_DEF(config, 32);
REG_BUF_REG_DEF(io, 32) = {'i', 'o', ':', '0', '0', '0', '0', '0'};
REG_BUF_DEF(io, 32);
REG_BUF_REG_DEF(eth0, 32) = {'e', 't', 'h', '0', ':', '0', '0', '0'};
REG_BUF_DEF(eth0, 32);
REG_BUF_REG_DEF(eth1, 32) = {'e', 't', 'h', '1', ':', '0', '0', '0'};
REG_BUF_DEF(eth1, 32);
REG_BUF_REG_DEF(eth2, 32) = {'e', 't', 'h', '2', ':', '0', '0', '0'};
REG_BUF_DEF(eth2, 32);
REG_BUF_REG_DEF(eth3, 32) = {'e', 't', 'h', '3', ':', '0', '0', '0'};
REG_BUF_DEF(eth3, 32);
static struct k_sem terminate_sem;

static bool dev_port_open(int cif_sock, uint8_t port, uint8_t *initial_tx, uint16_t initial_tx_len,
			  uint16_t max_rx_len)
{
	struct cif_raw_port_config port_cfg = {
		.port = port,
		.flags = CIF_PORT_FLG_ENABLE,
		.slave_max_recv_len = max_rx_len,
	};

	int ret = setsockopt(cif_sock, SOL_CIF_RAW, CIF_OPT_PORT, &port_cfg, sizeof(port_cfg));

	if (ret < 0) {
		LOG_ERR("Failed to configure CIF socket port, errno %d", errno);
		return false;
	}
	struct sockaddr_cif port_io = {
		.cif_family = AF_CIF,
		.port = port,
	};

	/* Initialize tx buffer with default ldp_a_header, and rx header to 0 */
	ret = sendto(cif_sock, initial_tx, initial_tx_len, 0, (struct sockaddr *)&port_io,
		     sizeof(port_io));
	if (ret < 0) {
		LOG_ERR("Failed to initialize tx buffer, errno %d", errno);
		return false;
	}
	return true;
}

static void deal_ethernet_data(int sock, uint8_t port, uint8_t *addr, uint16_t len)
{
	static uint8_t rx_buf[64];
	int ret;
	struct sockaddr_cif port_addr = {
		.cif_family = AF_CIF,
		.port = port,
	};
	socklen_t sl = sizeof(port_addr);

	if (len < 8) {
		LOG_ERR("Invalid data length %d", len);
		return;
	}

	/* Update the tx buffer */
	addr[0] = 'e';
	addr[1] = 't';
	addr[2] = 'h';
	addr[3] = '0' + port - 0x60;
	addr[4] = ':';
	addr[len - 1]++;

	ret = sendto(sock, addr, len, 0, (struct sockaddr *)&port_addr, sl);
	if (ret < 0 && errno != EAGAIN && errno != ENXIO) {
		LOG_ERR("Failed to send data back, errno %d", errno);
	}

	ret = recvfrom(sock, rx_buf, sizeof(rx_buf), 0, (struct sockaddr *)&port_addr, &sl);
	if (ret < 0 && errno != EAGAIN && errno != ENXIO) {
		LOG_ERR("Failed to receive data, errno %d", errno);
	} else if (ret > 0) {
		LOG_INF("Received data from port %d", port);
		LOG_HEXDUMP_INF(rx_buf, ret, "Data:");
	}
}
static int slave_task(void)
{
	LOG_INF("Running in slave mode");
	/**
	 * Step 1: create a CIF socket
	 */
	int sock = socket(AF_CIF, SOCK_RAW, CIF_RAW_SLAVE);
	int ret;

	if (sock < 0) {
		LOG_ERR("Failed to create CIF socket, errno %d", errno);
		return -1;
	}

	/**
	 * Step 2: bind the CIF socket
	 */
	struct sockaddr_cif local = {
		.cif_family = AF_CIF,
		.bus = CIF_BUS_DEFAULT,
		.slot = -1, /* not used */
		.port = -1, /* not used */
	};

	ret = bind(sock, (struct sockaddr *)&local, sizeof(local));
	if (ret < 0) {
		LOG_ERR("Failed to bind CIF socket, errno %d", errno);
		close(sock);
		return -1;
	}

	/**
	 * Step 3: open config&io ports.
	 */
#if CONFIG_MCB_SYSTECH_HW_WORKAROUND
	LOG_WRN("Disc(port 0) shall not be configured by PS!");
	if (!dev_port_open(sock, PORT_ID_DISC, REG_BUF_REG(disc), REG_BUF_LEN(disc),
			   REG_BUF_LEN(disc))) {
		close(sock);
		return -1;
	}
#endif
	if (!dev_port_open(sock, PORT_ID_CFG, REG_BUF_REG(config), REG_BUF_LEN(config),
			   REG_BUF_LEN(config))) {
		LOG_ERR("Failed to open config port");
		close(sock);
		return -1;
	}
	if (!dev_port_open(sock, PORT_ID_IO, REG_BUF_REG(io), REG_BUF_LEN(io), REG_BUF_LEN(io))) {
		LOG_ERR("Failed to open io port");
		close(sock);
		return -1;
	}
	if (!dev_port_open(sock, PORT_ID_ETH0, REG_BUF_REG(eth0), REG_BUF_LEN(eth0),
			   REG_BUF_LEN(eth0))) {
		LOG_ERR("Failed to open eth0 port");
		close(sock);
		return -1;
	}
	if (!dev_port_open(sock, PORT_ID_ETH1, REG_BUF_REG(eth1), REG_BUF_LEN(eth1),
			   REG_BUF_LEN(eth1))) {
		LOG_ERR("Failed to open eth1 port");
		close(sock);
		return -1;
	}
	if (!dev_port_open(sock, PORT_ID_ETH2, REG_BUF_REG(eth2), REG_BUF_LEN(eth2),
			   REG_BUF_LEN(eth2))) {
		LOG_ERR("Failed to open eth2 port");
		close(sock);
		return -1;
	}
	if (!dev_port_open(sock, PORT_ID_ETH3, REG_BUF_REG(eth3), REG_BUF_LEN(eth3),
			   REG_BUF_LEN(eth3))) {
		LOG_ERR("Failed to open eth3 port");
		close(sock);
		return -1;
	}

	struct sockaddr_cif port_cfg = {.cif_family = AF_CIF, .port = PORT_ID_CFG};
	struct sockaddr_cif port_io = {
		.cif_family = AF_CIF,
		.port = PORT_ID_IO,
	};

	LOG_INF("Slave is ready to receive config data");

	/* Goto loop now */
	while (1) {
		socklen_t sl = sizeof(port_cfg);
		bool new_config = false;

		/* Prepare config data */
		ret = recvfrom(sock, REG_BUF_MODIFY(config), REG_BUF_LEN(config), 0,
			       (struct sockaddr *)&port_cfg, &sl);

		if (ret >= 0) {
			LOG_INF("Received config data");
			/* Do something with the config data */

			new_config = ret > 0 ? true : false;
		} else if (errno == EAGAIN) {
			/* Nothing to do */
		} else {
			LOG_ERR("Failed to receive config data, errno %d", errno);
		}

		/* If there is new config data, send it back */
		if (new_config) {
			/* Update the config data */
			memcpy(REG_BUF_REG(config), REG_BUF_MODIFY(config), REG_BUF_LEN(config));
			ret = sendto(sock, REG_BUF_REG(config), REG_BUF_LEN(config), 0,
				     (struct sockaddr *)&port_cfg, sizeof(port_cfg));
			if (ret >= 0) {
				LOG_INF("Sent config data back");
			} else if (errno == EAGAIN || ENXIO) {
				/* Nothing to do */
			} else {
				LOG_ERR("Failed to send config data, errno %d", errno);
			}
		}

		sl = sizeof(port_io);
		if (recvfrom(sock, REG_BUF_MODIFY(io), REG_BUF_LEN(io), 0,
			     (struct sockaddr *)&port_io, &sl) > 0) {
			LOG_INF("Received io data");

			/* Do something with the input data (this is only for demo, the actual
			 * application don't need to do this)
			 */
			if (REG_BUF_MODIFY(io)[0] != 'i' || REG_BUF_MODIFY(io)[1] != 'o') {
				LOG_INF("Invalid io data");
			} else {
				LOG_INF("Valid io data, slave start to do the dirty work");
			}
		}

		/* Prepare the output data */
		REG_BUF_REG(io)[5]++;
		ret = sendto(sock, REG_BUF_REG(io), REG_BUF_LEN(io), 0, (struct sockaddr *)&port_io,
			     sizeof(port_io));
		if (ret < 0) {
			LOG_INF("Failed to send io data, errno %d", errno);
		}

		deal_ethernet_data(sock, PORT_ID_ETH0, REG_BUF_MODIFY(eth0), REG_BUF_LEN(eth0));
		deal_ethernet_data(sock, PORT_ID_ETH1, REG_BUF_MODIFY(eth1), REG_BUF_LEN(eth1));
		deal_ethernet_data(sock, PORT_ID_ETH2, REG_BUF_MODIFY(eth2), REG_BUF_LEN(eth2));
		deal_ethernet_data(sock, PORT_ID_ETH3, REG_BUF_MODIFY(eth3), REG_BUF_LEN(eth3));

		k_usleep(10);

		/* NOTE: terminate condition */
		if (k_sem_take(&terminate_sem, K_NO_WAIT) == 0) {
			zsock_close(sock);
			break;
		}
	}
	return 0;
}

K_THREAD_STACK_DEFINE(slave_stack, 2048);
static struct k_thread slave_thread;

int slave_start(void)
{
	k_tid_t ret = k_thread_create(
		&slave_thread, slave_stack, K_THREAD_STACK_SIZEOF(slave_stack),
		(k_thread_entry_t)slave_task, NULL, NULL, NULL, K_PRIO_PREEMPT(2), 0, K_NO_WAIT);
	k_thread_name_set(&slave_thread, "slave");
	k_sem_init(&terminate_sem, 0, 1);
	return (int)ret;
}
int slave_cancel(void)
{
	k_sem_give(&terminate_sem);
	k_thread_join(&slave_thread, K_FOREVER);
	return 0;
}
