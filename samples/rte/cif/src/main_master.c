/**
 * @license This project is CLOSED SOURCE, All Rights Reserved
 * Copyright (c) 2025 SYSTech Co.
 */
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/socketcif.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#define MAX_DEV 2

#define MCB_POLL_TIME           (50 * 1000) /* 50us */
#define SYNC_CYCLE_TIME         2000        /* 2ms */
#define ASYNC_INTERVAL_TIME     10000       /* 10ms */
#define ASYNC_TIMEOUT_TIME      100000      /* 100ms */
#define ASYNC_DEFAULT_BANDWIDTH 0           /* no limitation */

#define PORT_ID_CFG 0x10
#define PORT_ID_IO  0x40

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

struct context {
	int target_sid;

#define CMD_NONE      0
#define CMD_DISCOVERY 1
#define CMD_CONFIG    2
#define CMD_IO        3
	uint32_t cmd;

#define STATE_IDLE     0
#define STATE_DISCOVER 1
#define STATE_CONFIG   2
#define STATE_IO_START 3
#define STATE_IO       4
	uint32_t state;
};

static struct context ctx_ = {0};

static void parse_cmd(void)
{
	switch (ctx_.cmd) {
	case CMD_DISCOVERY:
		ctx_.state = STATE_DISCOVER;
		break;
	case CMD_CONFIG:
		ctx_.state = STATE_CONFIG;
		break;
	case CMD_IO:
		ctx_.state = STATE_IO_START;
		break;
	case CMD_NONE:
	default:
		break;
	}
	ctx_.cmd = CMD_NONE;
}

static bool dev_discovery(int cif_sock, uint8_t slot)
{
	bool exist = true;

	struct cif_raw_port_config port_cfg = {
		.port = 0,
		.slot = slot,
		.flags = CIF_PORT_FLG_ENABLE | CIF_PORT_FLG_ONE_SHOT,
	};

	int ret = setsockopt(cif_sock, SOL_CIF_RAW, CIF_OPT_PORT, &port_cfg, sizeof(port_cfg));

	if (ret < 0) {
		LOG_ERR("Failed to configure CIF socket port, errno %d", errno);
		return false;
	}

	/* Wait for LDP query discovery response automatically */
	k_msleep(500);

	static uint8_t buf[CIF_MTU];
	struct sockaddr_cif addr = {
		.cif_family = AF_CIF,
		.slot = slot,
		.port = 0,
	};
	socklen_t addr_len = sizeof(addr);
	ssize_t len;

	len = recvfrom(cif_sock, buf, sizeof(buf), 0, (struct sockaddr *)&addr, &addr_len);

	if (len < 0) {
		LOG_ERR("Failed to receive data, errno %d", errno);
		exist = false;
	} else {
		LOG_ERR("Received %zd bytes", len);
		LOG_HEXDUMP_INF(buf, len, "Received data");
		exist = true;
	}

	/* close connection */
	port_cfg.flags &= ~CIF_PORT_FLG_ENABLE;
	ret = setsockopt(cif_sock, SOL_CIF_RAW, CIF_OPT_PORT, &port_cfg, sizeof(port_cfg));
	if (ret < 0) {
		LOG_ERR("Failed to configure CIF socket port, errno %d", errno);
	}

	return exist;
}

static bool dev_general_init(int cif_sock, uint8_t slot, uint8_t port, uint32_t ext_flg)
{
	struct cif_raw_port_config port_cfg = {
		.port = port,
		.slot = slot,
		.flags = CIF_PORT_FLG_ENABLE | ext_flg,

		/** async configuration */
		.async_interval = ASYNC_INTERVAL_TIME,
		.async_timeout = ASYNC_TIMEOUT_TIME,
		.async_bandwidth = ASYNC_DEFAULT_BANDWIDTH,
	};

	int ret = setsockopt(cif_sock, SOL_CIF_RAW, CIF_OPT_PORT, &port_cfg, sizeof(port_cfg));

	if (ret < 0) {
		LOG_ERR("Failed to configure CIF socket port, errno %d", errno);
		return false;
	}
	return true;
}

static bool dev_general_deinit(int cif_sock, uint8_t slot, uint8_t port)
{
	struct cif_raw_port_config port_cfg = {
		.port = port,
		.slot = slot,
		.flags = 0,
	};

	int ret = setsockopt(cif_sock, SOL_CIF_RAW, CIF_OPT_PORT, &port_cfg, sizeof(port_cfg));

	if (ret < 0) {
		LOG_ERR("Failed to configure CIF socket port, errno %d", errno);
		return false;
	}
	return true;
}

static bool dev_general_cfg(int cif_sock, uint8_t slot, uint8_t port, const void *cfg, size_t len)
{
	char response[64];
	struct sockaddr_cif remote = {
		.cif_family = AF_CIF,
		.slot = slot,
		.port = port,
	};
	socklen_t addrlen = sizeof(remote);
	int ret, max_try = 10;

	ret = sendto(cif_sock, cfg, len, 0, (struct sockaddr *)&remote, sizeof(remote));
	if (ret < 0) {
		LOG_ERR("Failed to send data, errno %d", errno);
		return false;
	}

	while (--max_try) {
		int len = recvfrom(cif_sock, response, sizeof(response), 0,
				   (struct sockaddr *)&remote, (socklen_t *)(&addrlen));
		if (len > 0) {
			LOG_INF("Received cfg response %d bytes", len);
			LOG_HEXDUMP_INF(response, len, "Received data");
			/* FIXME: Need to read data back configuration to check if it is set ok */
			return true;
		} else if (len < 0 && errno == EAGAIN) {
			LOG_DBG("Wait for configuration done");
		} else if (len < 0) {
			LOG_ERR("Failed to receive data, errno %d", errno);
			return false;
		}
		LOG_DBG("No response, retry %d", max_try);
		/* Must make sure that give enough time for LDP to schedule other cycles */
		k_msleep(1000);
	}
	return false;
}

int main(void)
{
	/**
	 * Step 1: create a CIF socket
	 */
	LOG_INF("--------------------------------------");
	LOG_INF("Testcase: Create Socket");
	LOG_INF("--------------------------------------");
	int sock = socket(AF_CIF, SOCK_RAW, CIF_RAW_MASTER);
	int ret;

	if (sock < 0) {
		LOG_ERR("Failed to create CIF socket, errno %d", errno);
		return -1;
	}
	LOG_INF("\n");
	/**
	 * Step 2: bind the CIF socket
	 */
	LOG_INF("--------------------------------------");
	LOG_INF("Testcase: Bind socket");
	LOG_INF("--------------------------------------");
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
	LOG_INF("\n");

	/**
	 * Step 3: set the CIF socket options
	 */
	LOG_INF("--------------------------------------");
	LOG_INF("Testcase: Set CIF options");
	LOG_INF("--------------------------------------");
	const struct cif_raw_master_config config = {
		.poll_time = MCB_POLL_TIME,
		.cycle_time = SYNC_CYCLE_TIME,
	};
	ret = setsockopt(sock, SOL_CIF_RAW, CIF_OPT_MASTER_CONFIG, &config, sizeof(config));
	if (ret < 0) {
		LOG_ERR("Failed to set CIF socket options, errno %d", errno);
		close(sock);
		return -1;
	}
	LOG_INF("\n");

	ctx_.state = STATE_IDLE;

	bool res;
	uint32_t cnt = 1;
	static uint8_t in_buf[64];

	while (1) {
		k_msleep(100);
		switch (ctx_.state) {
		case STATE_IDLE:
			parse_cmd();
			break;
		case STATE_DISCOVER:
			res = dev_discovery(sock, ctx_.target_sid);

			LOG_INF("Slot %d %s", ctx_.target_sid, res ? "exist" : "not exist");
			ctx_.state = STATE_IDLE;
			break;
		case STATE_CONFIG:

			bool res =
				dev_general_init(sock, ctx_.target_sid, PORT_ID_CFG,
						 CIF_PORT_FLG_STRONG_ORDER | CIF_PORT_FLG_ONE_SHOT);
			if (res) {
				static const char config_data[] = "cfg:123";

				res = dev_general_cfg(sock, ctx_.target_sid, PORT_ID_CFG,
						      config_data, sizeof(config_data));
				LOG_INF("Slot %d configuration %s", ctx_.target_sid,
					res ? "done" : "not done");
				dev_general_deinit(sock, ctx_.target_sid, PORT_ID_CFG);
			}

			ctx_.state = STATE_IDLE;
			break;

		case STATE_IO_START:
			res = dev_general_init(sock, ctx_.target_sid, PORT_ID_IO, 0);
			cnt = 10;

			if (res) {
				struct sockaddr_cif remote = {
					.cif_family = AF_CIF,
					.slot = ctx_.target_sid,
					.port = PORT_ID_IO,
				};
				ret = sendto(sock, "io:0", 4, 0, (struct sockaddr *)&remote,
					     sizeof(remote));
				if (ret < 0) {
					LOG_ERR("Failed to send data, errno %d", errno);
					ctx_.state = STATE_IDLE;
				}
			} else {
				LOG_ERR("Failed to establish connection to slot%d",
					ctx_.target_sid);
				ctx_.state = STATE_IDLE;
			}
			ctx_.state = STATE_IO;
			break;
		case STATE_IO:
			if (--cnt == 0) {
				ctx_.state = STATE_IDLE;
				dev_general_deinit(sock, ctx_.target_sid, PORT_ID_IO);
				continue;
			}

			ret = recvfrom(sock, in_buf, sizeof(in_buf), 0, NULL, 0);
			if (ret < 0) {
				LOG_ERR("Failed to receive data, errno %d", errno);
			} else {
				LOG_HEXDUMP_DBG(in_buf, ret, "Received data from slot");
			}
			break;
		}
	}

	while (1) {
		k_msleep(1000);
	}

	return 0;
}

static int cif_cmd(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (argc != 3) {
		shell_print(sh, "cif <cmd> <sid>");
		shell_print(sh, "  cmd: discovery, config, io");
		return 0;
	}

	if (!strcmp(argv[1], "discovery")) {
		ctx_.cmd = CMD_DISCOVERY;
	} else if (!strcmp(argv[1], "config")) {
		ctx_.cmd = CMD_CONFIG;
	} else if (!strcmp(argv[1], "io")) {
		ctx_.cmd = CMD_IO;
	} else {
		shell_print(sh, "Invalid command");
		return -EINVAL;
	}

	ctx_.target_sid = atoi(argv[2]);

	return 0;
}

SHELL_CMD_REGISTER(cif, NULL, "Dump version information", cif_cmd);
