/**
 * @license This project is CLOSED SOURCE, All Rights Reserved
 * Copyright (c) 2025 SYSTech Co.
 */
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/socketcif.h>
#include <zephyr/logging/log.h>

#include "cif_main.h"

LOG_MODULE_REGISTER(main_m, CONFIG_CIF_LOG_LEVEL);

extern struct context ctx_;

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

	static uint8_t buf[CIF_MTU];
	struct sockaddr_cif addr = {
		.cif_family = AF_CIF,
		.slot = slot,
		.port = 0,
	};
	socklen_t addr_len = sizeof(addr);
	ssize_t len;

	/* NOTE: User Call rx() to trigger LDP start to transmit */
	recvfrom(cif_sock, buf, sizeof(buf), 0, (struct sockaddr *)&addr, &addr_len);

	/* Wait for LDP query discovery response automatically */
	k_usleep(SYNC_CYCLE_TIME * 2);

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
		k_usleep(ASYNC_INTERVAL_TIME);
	}
	return false;
}

static int handle_extra_errors(int sock)
{
	int ret;
	struct cif_error_filter err_mask = {
		.flags = 0,
	};
	socklen_t err_len = sizeof(err_mask);

	ret = getsockopt(sock, SOL_CIF_RAW, CIF_OPT_ERROR, &err_mask, &err_len);
	if (ret < 0 && errno != ENOENT) {
		LOG_WRN("Failed to get error mask, errno %d", errno);
	} else if (ret < 0 && errno == ENOENT) {
		/* No activated connection found */
		return 0;
	}

	ret = setsockopt(sock, SOL_CIF_RAW, CIF_OPT_ERROR, &err_mask, sizeof(err_mask));
	if (ret < 0) {
		LOG_WRN("Failed to clear error mask, errno %d", errno);
	}

	if (err_mask.error_mask) {
		if (err_mask.error_mask & (CIF_ERR_PREV_PREEMPT | CIF_ERR_PREEMPT)) {
			LOG_WRN("Preempt error detected, switch to slave mode");
			/* Close socket will release all resources */
			ctx_.target_role = role_slave;
			ctx_.target_opt = normal;
			ctx_.state = STATE_IDLE;
			zsock_close(sock);
			slave_start();
			return 1;
		} else if (err_mask.error_mask & CIF_ERR_R_ERROR) {
			LOG_WRN("R_ERROR detected");
		} else if (err_mask.error_mask & CIF_ERR_I_ERROR) {
			LOG_WRN("I_ERROR detected");
		} else if (err_mask.error_mask & CIF_ERR_MAY_LOST) {
			LOG_WRN("MAY_LOST error detected");
		} else if (err_mask.error_mask & CIF_ERR_PREV_T_ERROR) {
			LOG_WRN("PREV_T_ERROR detected");
		} else if (err_mask.error_mask & CIF_ERR_PREV_R_ERROR) {
			LOG_WRN("PREV_R_ERROR detected");
		} else if (err_mask.error_mask & CIF_ERR_PREV_I_ERROR) {
			LOG_WRN("PREV_I_ERROR detected");
		} else if (err_mask.error_mask & CIF_ERR_PREV_P_ERROR) {
			LOG_WRN("PREV_P_ERROR detected");
		} else if (err_mask.error_mask & CIF_ERR_PREV_MAY_LOST) {
			LOG_WRN("PREV_MAY_LOST detected");
		} else if (err_mask.error_mask & CIF_ERR_PREV_INVALID_ASYNC_PACK) {
			LOG_WRN("PREV_INVALID_ASYNC_PACK detected");
		} else {
			LOG_ERR("Unknown error mask 0x%08x detected", err_mask.error_mask);
		}
	}
	return 0;
}

static int main_master(void)
{

	LOG_INF("Running in master mode");
	/**
	 * Step 1: create a CIF socket
	 */
	int sock = socket(AF_CIF, SOCK_RAW, CIF_RAW_MASTER);
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
	 * Step 3: set the CIF socket options
	 */
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

	ctx_.state = STATE_IDLE;

	bool res;
	uint32_t cnt = 1;
	uint32_t flags = 0;
	static uint8_t in_buf[512];

	while (1) {

		/* Terminate condition */
		if (k_sem_take(&ctx_.terminate_sem, K_NO_WAIT) == 0) {
			zsock_close(sock);
			break;
		}

		switch (ctx_.state) {
		case STATE_IDLE:
			parse_cmd();
			/* Give up CPU and hand over */
			k_usleep(SYNC_CYCLE_TIME);
			break;
		case STATE_DISCOVER:
			res = dev_discovery(sock, ctx_.target_sid);

			LOG_INF("Slot %d %s", ctx_.target_sid, res ? "exist" : "not exist");
			ctx_.state = STATE_IDLE;
			break;
		case STATE_CONFIG:
			flags = CIF_PORT_FLG_STRONG_ORDER | CIF_PORT_FLG_ONE_SHOT;

			bool res = dev_general_init(
				sock, ctx_.target_sid, PORT_ID_CFG,
				ctx_.target_opt == normal ? flags : flags | CIF_PORT_FLG_PREEMPT);

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
			res = dev_general_init(sock, ctx_.target_sid, ctx_.target_port,
					       ctx_.target_opt == preempt ? CIF_PORT_FLG_PREEMPT
									  : 0);
			cnt = ctx_.target_cnt + 1;
			if (res) {
				struct sockaddr_cif remote = {
					.cif_family = AF_CIF,
					.slot = ctx_.target_sid,
					.port = ctx_.target_port,
				};
				ctx_.curr.cif_family = remote.cif_family;
				ctx_.curr.slot = remote.slot;
				ctx_.curr.port = remote.port;
				ctx_.curr_len = sizeof(ctx_.curr);
				ctx_.stat_ok = 0;
				ctx_.stat_failed = 0;
				ctx_.systick_begin = sys_clock_tick_get();
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
			if (--cnt > 0) {
				k_usleep(SYNC_CYCLE_TIME);
				ret = recvfrom(sock, in_buf, sizeof(in_buf), 0,
					       (struct sockaddr *)&ctx_.curr, &ctx_.curr_len);
				if (ret < 0) {
					LOG_ERR("Failed to receive data, errno %d", errno);
					ctx_.stat_failed++;
				} else {
					LOG_HEXDUMP_DBG(in_buf, ret, "Received data from slot");
					ctx_.stat_ok++;
				}
			} else {
				ctx_.systick_end = sys_clock_tick_get();
				ctx_.state = STATE_IDLE;
				dev_general_deinit(sock, ctx_.target_sid, ctx_.target_port);

				LOG_INF("IO tested, package passed: %d err: %d, percentage: %3d%%",
					ctx_.stat_ok, ctx_.stat_failed,
					ctx_.stat_ok * 100 / (ctx_.stat_ok + ctx_.stat_failed));
				LOG_INF("Duration: %lldms",
					(ctx_.systick_end - ctx_.systick_begin));
			}
			break;
		}

		if (handle_extra_errors(sock)) {
			break;
		}
	}

	return 0;
}

static struct k_thread master_thread;
static K_THREAD_STACK_DEFINE(master_stack, 4096);

int master_start(void)
{
	k_sem_init(&ctx_.terminate_sem, 0, 1);
	k_tid_t tid = k_thread_create(
		&master_thread, master_stack, K_THREAD_STACK_SIZEOF(master_stack),
		(k_thread_entry_t)main_master, NULL, NULL, NULL, K_PRIO_PREEMPT(10), 0, K_NO_WAIT);

	k_thread_name_set(&master_thread, "master");

	if (tid == NULL) {
		LOG_ERR("Failed to create master thread");
		return -1;
	}

	return 0;
}

int master_cancel(void)
{
	k_sem_give(&ctx_.terminate_sem);
	k_thread_join(&master_thread, K_FOREVER);
	return 0;
}
