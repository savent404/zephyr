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

extern struct context ctx_;

struct reg_buf {
	uint8_t *reg;    /* Actual register buffer */
	uint8_t *modify; /* User modified buffer */
	uint16_t len;    /* Register length */
};

REG_BUF_REG_DEF(config, 64) = {'c', 'f', 'g', ':', '0', '0', '0', '0'};
REG_BUF_DEF(config, 64);
REG_BUF_REG_DEF(io, 128) = {'i', 'o', ':', '0', '0', '0', '0', '0'};
REG_BUF_DEF(io, 128);
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
		.flags = CIF_PORT_FLG_ENABLE | CIF_PORT_FLG_ALLOW_WRITE,
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

static int deal_ethernet_data(int sock, uint8_t port)
{
	static uint8_t rx_buf[CIF_ASYNC_MTU];
	int ret;
	struct sockaddr_cif port_addr = {
		.cif_family = AF_CIF,
		.port = port,
	};
	socklen_t sl = sizeof(port_addr);
	bool something2do = false;

	if (!ctx_.perf_mode) {
		/* don't care about the performance, using echo to validate loopback */
		ret = recvfrom(sock, rx_buf, sizeof(rx_buf), 0, (struct sockaddr *)&port_addr, &sl);
		if (ret < 0 && errno != EAGAIN) {
			LOG_ERR("Failed to receive data, errno %d", errno);
		} else if (ret > 0) {
			LOG_INF("Received data from port %d", port);
			LOG_HEXDUMP_INF(rx_buf, ret, "Data:");

#if !CONFIG_CIF_SLAVE_ONLY_RECV
			ret = sendto(sock, rx_buf, ret, 0, (struct sockaddr *)&port_addr, sl);
			if (ret < 0 && errno != EAGAIN) {
				LOG_ERR("Failed to send data back, errno %d", errno);
			}
#endif

			something2do = true;
		}
	} else {
		/* performance mode, send any data to the port */
		ret = recvfrom(sock, rx_buf, sizeof(rx_buf), 0, (struct sockaddr *)&port_addr, &sl);

		if (ret < 0 && errno != EAGAIN) {
			LOG_ERR("Failed to receive data, errno %d", errno);
		} else if (ret > 0) {
			LOG_INF("Received data from port %d", port);
			LOG_HEXDUMP_INF(rx_buf, ret, "Data:");
			something2do = true;
		}

#if !CONFIG_CIF_SLAVE_ONLY_RECV
		ret = sendto(sock, rx_buf, 1500, 0, (struct sockaddr *)&port_addr, sl);
		if (ret < 0 && errno != EAGAIN) {
			LOG_ERR("Failed to send data back, errno %d", errno);
		} else if (ret > 0) {
			LOG_INF("Sent data back to port %d", port);
			something2do = true;
		}
#endif
	}

	return something2do ? 1 : 0;
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
		.bus = ctx_.target_bus == bus_low ? CIF_BUS_SLOW : CIF_BUS_FAST,
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
	 * Step 3: set the CIF socket options
	 */
	struct cif_raw_slave_config opt = {
		.want_preempt = ctx_.target_opt == preempt ? 1 : 0,
		.dr = CIF_DR_MPU_B,
		.dt = CIF_DT_MPU_B,
	};
	ret = setsockopt(sock, SOL_CIF_RAW, CIF_OPT_SLAVE_CONFIG, &opt, sizeof(opt));
	if (ret < 0) {
		LOG_ERR("Failed to set CIF socket options, errno %d", errno);
		close(sock);
		return -1;
	}

	/* Step 4: Detect the device information */
	struct cif_info info;
	socklen_t cif_info_len = sizeof(info);

	ret = getsockopt(sock, SOL_CIF_RAW, CIF_OPT_INFO, &info, &cif_info_len);
	if (ret < 0) {
		LOG_ERR("Failed to get CIF socket info, errno %d", errno);
		close(sock);
		return -1;
	}
	LOG_INF("CIF socket info: slot %d, hw version: %d", info.slot, info.hw_version);

	/**
	 * Step 5: open config&io ports.
	 */
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
			   CIF_ASYNC_MTU)) {
		LOG_ERR("Failed to open eth0 port");
		close(sock);
		return -1;
	}
	if (!dev_port_open(sock, PORT_ID_ETH1, REG_BUF_REG(eth1), REG_BUF_LEN(eth1),
			   CIF_ASYNC_MTU)) {
		LOG_ERR("Failed to open eth1 port");
		close(sock);
		return -1;
	}
	if (!dev_port_open(sock, PORT_ID_ETH2, REG_BUF_REG(eth2), REG_BUF_LEN(eth2),
			   CIF_ASYNC_MTU)) {
		LOG_ERR("Failed to open eth2 port");
		close(sock);
		return -1;
	}
	if (!dev_port_open(sock, PORT_ID_ETH3, REG_BUF_REG(eth3), REG_BUF_LEN(eth3),
			   CIF_ASYNC_MTU)) {
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
			} else if (errno == EAGAIN) {
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
			memcpy(REG_BUF_REG(io), REG_BUF_MODIFY(io), REG_BUF_LEN(io));
		}

		/* Prepare the output data */
		REG_BUF_REG(io)[5]++;
		ret = sendto(sock, REG_BUF_REG(io), REG_BUF_LEN(io), 0, (struct sockaddr *)&port_io,
			     sizeof(port_io));
		if (ret < 0) {
			LOG_INF("Failed to send io data, errno %d", errno);
		}

		int no_job = 0;

		no_job += deal_ethernet_data(sock, PORT_ID_ETH0);
		no_job += deal_ethernet_data(sock, PORT_ID_ETH1);
		no_job += deal_ethernet_data(sock, PORT_ID_ETH2);
		no_job += deal_ethernet_data(sock, PORT_ID_ETH3);

		if (no_job == 0) {
			k_usleep(10);
		}

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
