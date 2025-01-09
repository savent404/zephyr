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

#if !CONFIG_CIF_WORKAROUND
#define PORT_ID_CFG 0x10
#define PORT_ID_IO  0x40
#else
#define PORT_ID_CFG 1
#define PORT_ID_IO  2
#endif

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

REG_BUF_REG_DEF(config, 8) = {'c', 'f', 'g', ':', '0', '0', '0', '0'};
REG_BUF_DEF(config, 8);
REG_BUF_REG_DEF(io, 8) = {'i', 'o', ':', '0', '0', '0', '0', '0'};
REG_BUF_DEF(io, 8);

static bool dev_port_open(int cif_sock, uint8_t port, uint8_t *initial_tx, uint16_t initial_tx_len,
			  uint16_t max_rx_len)
{
	struct cif_raw_port_config port_cfg = {
		.port = port,
		.flags = CIF_PORT_FLG_ENABLE,
	};

	int ret = setsockopt(cif_sock, SOL_CIF_RAW, CIF_OPT_PORT, &port_cfg, sizeof(port_cfg));

	if (ret < 0) {
		LOG_INF("Failed to configure CIF socket port, errno %d\n", errno);
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
		LOG_INF("Failed to create CIF socket, errno %d\n", errno);
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
		LOG_INF("Failed to bind CIF socket, errno %d\n", errno);
		close(sock);
		return -1;
	}

	/**
	 * Step 3: open config&io ports.
	 */
	if (!dev_port_open(sock, PORT_ID_CFG, REG_BUF_REG(config), REG_BUF_LEN(config),
			   REG_BUF_LEN(io))) {
		LOG_INF("Failed to open config port\n");
		close(sock);
		return -1;
	}
	if (!dev_port_open(sock, PORT_ID_IO, REG_BUF_REG(io), REG_BUF_LEN(io),
			   REG_BUF_LEN(config))) {
		LOG_INF("Failed to open io port\n");
		close(sock);
		return -1;
	}

	struct sockaddr_cif port_cfg = {.cif_family = AF_CIF, .port = PORT_ID_CFG};
	struct sockaddr_cif port_io = {
		.cif_family = AF_CIF,
		.port = PORT_ID_IO,
	};

	/* Goto loop now */
	while (1) {
		socklen_t sl = sizeof(port_cfg);

		ret = recvfrom(sock, REG_BUF_MODIFY(config), REG_BUF_LEN(config), 0,
			       (struct sockaddr *)&port_cfg, &sl);

		if (ret > 0) {
			LOG_INF("Received config data\n");
			/* Do something with the config data */

			/* Update the config data */
			memcpy(REG_BUF_REG(config), REG_BUF_MODIFY(config), REG_BUF_LEN(config));
			ret = sendto(sock, REG_BUF_REG(config), REG_BUF_LEN(config), 0,
				     (struct sockaddr *)&port_cfg, sizeof(port_cfg));
			if (ret < 0) {
				LOG_INF("Failed to send config data, errno %d\n", errno);
			}
		} else if (ret == -EAGAIN) {
			/* Nothing to do */
		} else {
			LOG_INF("Failed to receive config data, errno %d\n", errno);
		}

		sl = sizeof(port_io);
		if (recvfrom(sock, REG_BUF_MODIFY(io), REG_BUF_LEN(io), 0,
			     (struct sockaddr *)&port_io, &sl) > 0) {
			LOG_INF("Received io data\n");

			/* Do something with the input data (this is only for demo, the actual
			 * application don't need to do this)
			 */
			if (REG_BUF_MODIFY(io)[0] != 'i' || REG_BUF_MODIFY(io)[1] != 'o') {
				LOG_INF("Invalid io data\n");
			} else {
				LOG_INF("Valid io data, slave start to do the dirty work\n");
			}
		}

		/* Prepare the output data */
		REG_BUF_REG(io)[5]++;
		ret = sendto(sock, REG_BUF_REG(io), REG_BUF_LEN(io), 0, (struct sockaddr *)&port_io,
			     sizeof(port_io));
		if (ret < 0) {
			LOG_INF("Failed to send io data, errno %d\n", errno);
		}
	}
}
