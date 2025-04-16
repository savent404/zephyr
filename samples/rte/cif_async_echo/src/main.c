/**
 * @file main.c
 * @author Liao YuanKai(savent_gate@outlook.com)
 * @brief
 * @version 0.1
 * @date 2025-04-16
 *
 * @copyright Copyright (c) 2025 SYSTech Co.
 *
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/socketcif.h>
#include <string.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(main, 3);

#define MCB_POLL_TIME           (655 * 1000)          /* 655us */
#define SYNC_CYCLE_TIME         20000                 /* 20ms */
#define SYNC_TIMEOUT_TIME       (SYNC_CYCLE_TIME * 5) /* 100ms (5 times of SYNC_CYCLE_TIME) */
#define ASYNC_INTERVAL_TIME     (40 * 1000)           /* 40ms */
#define ASYNC_TIMEOUT_TIME      (200 * 1000)          /* 200ms */
#define ASYNC_DEFAULT_BANDWIDTH 0                     /* no limitation */

static int slot_detect(void);
static int master_init(struct sockaddr_cif *port_addr);
static int slave_init(struct sockaddr_cif *port_addr);
static int handle_extra_errors(int sock);
static void handle_recv_msg(char *buf, int len, bool is_master);

int main(void)
{
	struct sockaddr_cif addr;
	socklen_t sl = sizeof(addr);
	bool is_master;
	int sock;
	static char buf[1500];

	is_master = slot_detect() == 1 ? true : false;

	if (is_master) {
		sock = master_init(&addr);
	} else {
		sock = slave_init(&addr);
	}

	if (sock < 0) {
		LOG_ERR("Failed to create socket, errno %d", errno);
	}

	if (is_master) {
		/* send initial data to slave */
		memset(buf, 0, sizeof(buf));
		if (sendto(sock, buf, sizeof(buf), 0, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			LOG_ERR("Failed to send initial data, errno %d", errno);
			return -1;
		}
	}

	/* Start the main loop */
	while (1) {
		int rc;

		do {
			rc = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&addr, &sl);
			if (rc > 0) {

				handle_recv_msg(buf, rc, is_master);

				rc = sendto(sock, buf, rc, 0, (struct sockaddr *)&addr, sl);
				if (rc < 0) {
					LOG_WRN("Failed to send data, errno %d", errno);
				}
			} else {
				if (errno == EAGAIN) {
					LOG_DBG("Nothing to do, continue");
				} else {
					LOG_ERR("Failed to receive data, errno %d", errno);
				}
				break;
			}
		} while (1);
		handle_extra_errors(sock);
		k_sleep(is_master ? K_SECONDS(1) : K_MSEC(1));
	}
}

static int slot_detect(void)
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
		.bus = CIF_BUS_SLOW,
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
		.want_preempt = 0,
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

	close(sock);

	return info.slot;
}

static int master_init(struct sockaddr_cif *port_addr)
{
	/* Step 1: create a CIF socket */
	int sock = socket(AF_CIF, SOCK_RAW, CIF_RAW_MASTER);
	if (sock < 0) {
		LOG_ERR("Failed to create CIF socket, errno %d", errno);
		return -1;
	}

	/* Step 2: bind the CIF socket */
	struct sockaddr_cif local = {
		.cif_family = AF_CIF,
		.bus = CIF_BUS_SLOW,
		.slot = -1, /* not used */
		.port = -1, /* not used */
	};

	int ret = bind(sock, (struct sockaddr *)&local, sizeof(local));

	if (ret < 0) {
		LOG_ERR("Failed to bind CIF socket, errno %d", errno);
		close(sock);
		return -1;
	}

	/* Step 3: set the CIF socket options */
	const struct cif_raw_master_config config = {
		.poll_time = MCB_POLL_TIME,
		.cycle_time = SYNC_CYCLE_TIME,
		.sync_timeout = SYNC_TIMEOUT_TIME,
		.dr = CIF_DR_MPU_P,
		.dt = CIF_DT_MPU_P,
	};

	ret = setsockopt(sock, SOL_CIF_RAW, CIF_OPT_MASTER_CONFIG, &config, sizeof(config));
	if (ret < 0) {
		LOG_ERR("Failed to set CIF socket options, errno %d", errno);
		close(sock);
		return -1;
	}

	/* open port 16, sid=3 */
	struct cif_raw_port_config port_cfg = {
		.port = 16,
		.slot = 3,
		.flags = CIF_PORT_FLG_ENABLE,
		.async_interval = ASYNC_INTERVAL_TIME,
		.async_timeout = ASYNC_TIMEOUT_TIME,
		.async_bandwidth = ASYNC_DEFAULT_BANDWIDTH,
	};

	ret = setsockopt(sock, SOL_CIF_RAW, CIF_OPT_PORT, &port_cfg, sizeof(port_cfg));
	if (ret < 0) {
		LOG_ERR("Failed to set CIF socket options, errno %d", errno);
		close(sock);
		return -1;
	}

	memset(port_addr, 0, sizeof(*port_addr));
	port_addr->cif_family = AF_CIF;
	port_addr->slot = 3;  /* Slot number */
	port_addr->port = 16; /* Port number */

	return sock;
}

static int slave_init(struct sockaddr_cif *port_addr)
{
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
		.bus = CIF_BUS_SLOW,
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
		.want_preempt = 0,
		.dr = CIF_DR_MPU_B,
		.dt = CIF_DT_MPU_B,
	};

	ret = setsockopt(sock, SOL_CIF_RAW, CIF_OPT_SLAVE_CONFIG, &opt, sizeof(opt));
	if (ret < 0) {
		LOG_ERR("Failed to set CIF socket options, errno %d", errno);
		close(sock);
		return -1;
	}

	/* Open port 16 */
	struct cif_raw_port_config port_cfg = {
		.port = 16,
		.flags = CIF_PORT_FLG_ENABLE | CIF_PORT_FLG_ALLOW_WRITE,
		.slave_max_recv_len = 2000,
	};

	ret = setsockopt(sock, SOL_CIF_RAW, CIF_OPT_PORT, &port_cfg, sizeof(port_cfg));
	if (ret < 0) {
		LOG_ERR("Failed to set CIF socket options, errno %d", errno);
		close(sock);
		return -1;
	}

	memset(port_addr, 0, sizeof(*port_addr));
	port_addr->cif_family = AF_CIF;
	port_addr->port = 16; /* Port number */
	return sock;
}

static void handle_recv_msg(char *buf, int len, bool is_master)
{
	static uint8_t prev_id = 0xFF;
	uint8_t curr_id = buf[0];

	LOG_INF("Verify the buffer data, id: %d, len: %d", curr_id, len);

	/* case 1: all the bytes in the buffer are the same */
	for (int i = 0; i < len; i++) {
		if (buf[i] != curr_id) {
			LOG_WRN("Buffer data is not the same, id: %d, buf[%d] is %d", curr_id, i,
				buf[i]);
			break;
		}
	}

	/* case 2: the id is not changed against the previous one */
	if (curr_id == prev_id) {
		LOG_WRN("Buffer data is not changed, id: %d", curr_id);
	} else {
		LOG_DBG("Buffer data is changed, from %d to %d", prev_id, curr_id);
	}

	/* case 3: the expected length of msg is 1500 */
	if (len != 1500) {
		LOG_WRN("Buffer data length is not 1500, len: %d", len);
	} else {
		LOG_DBG("Buffer data length is 1500, len: %d", len);
	}

	prev_id = curr_id;

	if (!is_master) {

		/* update the buffer data with new id */
		curr_id = (curr_id + 1) % 256;
		for (int i = 0; i < len; i++) {
			buf[i] = curr_id;
		}
	}
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
		} else if (err_mask.error_mask & (CIF_ERR_R_ERROR | CIF_ERR_I_ERROR)) {
			struct cif_info info;
			int rc;

			LOG_WRN("I_ERROR/R_ERROR detected");
			err_len = sizeof(info);
			rc = getsockopt(sock, SOL_CIF_RAW, CIF_OPT_INFO, &info, &err_len);
			if (rc < 0) {
				LOG_ERR("Failed to get CIF socket info, errno %d", errno);
			} else {
				LOG_INF("CIF socket info: i_err[0]: %d, i_err[1]: %d",
					info.i_err[0], info.i_err[1]);
			}
		} else if (err_mask.error_mask & CIF_ERR_MAY_LOST) {
			LOG_WRN_ONCE("MAY_LOST error detected");
		} else if (err_mask.error_mask & CIF_ERR_PREV_T_ERROR) {
			LOG_WRN_ONCE("PREV_T_ERROR detected");
		} else if (err_mask.error_mask & CIF_ERR_PREV_R_ERROR) {
			LOG_WRN_ONCE("PREV_R_ERROR detected");
		} else if (err_mask.error_mask & CIF_ERR_PREV_I_ERROR) {
			LOG_WRN_ONCE("PREV_I_ERROR detected");
		} else if (err_mask.error_mask & CIF_ERR_PREV_P_ERROR) {
			LOG_WRN_ONCE("PREV_P_ERROR detected");
		} else if (err_mask.error_mask & CIF_ERR_PREV_MAY_LOST) {
			LOG_WRN_ONCE("PREV_MAY_LOST detected");
		} else if (err_mask.error_mask & CIF_ERR_PREV_INVALID_ASYNC_PACK) {
			LOG_WRN_ONCE("PREV_INVALID_ASYNC_PACK detected");
		} else if (err_mask.error_mask & CIF_ERR_PREV_RX_DROP_NOMEM) {
			LOG_WRN_ONCE("PREV_RX_DROP_NOMEM detected");
		} else if (err_mask.error_mask & CIF_ERR_PREV_RX_DROP_FIFO_FULL) {
			LOG_WRN_ONCE("PREV_RX_DROP_FIFO_FULL detected");
		} else if (err_mask.error_mask & CIF_ERR_PREV_RX_DROP_DUPLICATE) {
			LOG_WRN_ONCE("PREV_RX_DROP_DUPLICATE detected");
		} else if (err_mask.error_mask & CIF_ERR_PREV_RX_DROP_INVALID) {
			LOG_WRN_ONCE("PREV_RX_DROP_INVALID detected");
		} else if (err_mask.error_mask & CIF_ERR_PREV_ATIMEOUT) {
			LOG_WRN_ONCE("PREV_ATIMEOUT detected");
		} else {
			LOG_ERR("Unknown error mask 0x%08x detected", err_mask.error_mask);
		}
	}
	return 0;
}
