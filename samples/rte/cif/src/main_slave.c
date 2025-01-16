/**
 * @license This project is CLOSED SOURCE, All Rights Reserved
 * Copyright (c) 2025 SYSTech Co.
 */
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/socketcif.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

struct reg_buf {
	uint8_t *reg;    /* Actual register buffer */
	uint8_t *modify; /* User modified buffer */
	uint16_t len;    /* Register length */
};

#if CONFIG_MCB_SYSTECH_HW_WORKAROUND
#define PORT_ID_DISC 0x00
#endif
#define PORT_ID_CFG 0x10
#define PORT_ID_IO  0x40

/* Make sure that compile is satisfied */
#define REG_BUF_REG_DEF(name, length)                                                              \
	static uint8_t name##_modify[length];                                                      \
	static uint8_t name##_reg[length]
#define REG_BUF_LEN(name)    name.len
#define REG_BUF_REG(name)    name.reg
#define REG_BUF_MODIFY(name) name.modify
#define REG_BUF_DEF(name, length)                                                                  \
	static struct reg_buf name = {                                                             \
		.reg = (uint8_t *)&name##_reg,                                                     \
		.modify = (uint8_t *)&name##_modify,                                               \
		.len = length,                                                                     \
	}

#if CONFIG_MCB_SYSTECH_HW_WORKAROUND
REG_BUF_REG_DEF(disc, 32) = {'d', 'i', 's', 'c', ':', '0', '0', '0'};
REG_BUF_DEF(disc, 32);
#endif
REG_BUF_REG_DEF(config, 32) = {'c', 'f', 'g', ':', '0', '0', '0', '0'};
REG_BUF_DEF(config, 32);
REG_BUF_REG_DEF(io, 32) = {'i', 'o', ':', '0', '0', '0', '0', '0'};
REG_BUF_DEF(io, 32);

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

int main(void)
{
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
			   REG_BUF_LEN(io))) {
		LOG_ERR("Failed to open config port");
		close(sock);
		return -1;
	}
	if (!dev_port_open(sock, PORT_ID_IO, REG_BUF_REG(io), REG_BUF_LEN(io),
			   REG_BUF_LEN(config))) {
		LOG_ERR("Failed to open io port");
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

		k_msleep(10);
	}
}
