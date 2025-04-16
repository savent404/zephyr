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

/* Timing configurations */
#define MCB_POLL_TIME           (655 * 1000)          /* 655us */
#define SYNC_CYCLE_TIME         20000                 /* 20ms */
#define SYNC_TIMEOUT_TIME       (SYNC_CYCLE_TIME * 5) /* 100ms (5 times of SYNC_CYCLE_TIME) */
#define ASYNC_INTERVAL_TIME     (40 * 1000)           /* 40ms */
#define ASYNC_TIMEOUT_TIME      (200 * 1000)          /* 200ms */
#define ASYNC_DEFAULT_BANDWIDTH 0                     /* no limitation */

/* Port definitions */
#define MAX_PORTS        4
#define PORT_IDX_SLOW_16 0 /* Port 16 on slow bus */
#define PORT_IDX_FAST_16 1 /* Port 16 on fast bus */
#define PORT_IDX_SLOW_1  2 /* Port 1 on slow bus */
#define PORT_IDX_FAST_1  3 /* Port 1 on fast bus */

/* Protocol modes */
#define MODE_MASTER 1
#define MODE_SLAVE  0

/* Buffer sizes */
#define ASYNC_BUF_SIZE 1500 /* Buffer size for async ports */
#define SYNC_BUF_SIZE  256  /* Buffer size for sync ports */
#define MAX_BUF_SIZE   1500 /* Maximum buffer size (for static allocation) */

/* Helper function to check if port is sync or async */
#define IS_SYNC_PORT(port)  ((port) < 0x08)
#define IS_ASYNC_PORT(port) ((port) >= 0x08)

/* Message status codes */
#define MSG_STATUS_OK           0 /* Message processed normally */
#define MSG_STATUS_DUP_ID       1 /* Duplicate ID detected */
#define MSG_STATUS_INVALID_DATA 2 /* Data in message is not consistent */
#define MSG_STATUS_WRONG_SIZE   3 /* Message size doesn't match expected */

/**
 * @brief Structure to store message processing result
 */
struct msg_result {
	uint8_t prev_id; /* Previous ID (updated) */
	int status;      /* Status code (MSG_STATUS_*) */
};

/**
 * @brief Structure to store port configuration and state
 */
struct port_config {
	int sock;                 /* Socket for this bus */
	struct sockaddr_cif addr; /* Address for this port */
	int bus;                  /* CIF_BUS_SLOW or CIF_BUS_FAST */
	int port;                 /* Port number */
	int slot;                 /* Slot number, will be updated after detection */
	uint8_t prev_id;          /* Previous ID for message verification */
	bool is_active;           /* Is this port active */
};

/* Array to store all port configurations */
static struct port_config ports[MAX_PORTS] = {
	{-1, {0}, CIF_BUS_SLOW, 16, -1, 0xFF, false},
	{-1, {0}, CIF_BUS_FAST, 16, -1, 0xFF, false},
	{-1, {0}, CIF_BUS_SLOW, 1, -1, 0xFF, false},
	{-1, {0}, CIF_BUS_FAST, 1, -1, 0xFF, false},
};

/* Function prototypes */
static int slot_detect(void);
static int init_ports(int mode);
static int init_cif_socket(int mode, int bus_type);
static void set_port_activity(void);
static int handle_extra_errors(int sock);
static struct msg_result handle_recv_msg(char *buf, int len, bool is_master, uint8_t prev_id,
					 int port);
static void log_port_activity(int bus, int port, int status, int id, const char *action);
static int get_buffer_size_for_port(int port);

int main(void)
{
	socklen_t sl = sizeof(struct sockaddr_cif);
	static char buf[MAX_BUF_SIZE];
	bool is_master;
	int rc, i, buf_size;
	struct msg_result result;

	/* Detect slot and determine mode (master/slave) */
	int slot = slot_detect();

	if (slot < 0) {
		LOG_ERR("Failed to detect slot");
		return -1;
	}

	is_master = (slot == 1);
	LOG_INF("Running in %s mode", is_master ? "master" : "slave");

	/* Initialize ports based on mode */
	if (init_ports(is_master ? MODE_MASTER : MODE_SLAVE) < 0) {
		LOG_ERR("Failed to initialize ports");
		return -1;
	}

	/* Send initial data to all active ports if master */
	if (is_master) {
		for (i = 0; i < MAX_PORTS; i++) {
			if (ports[i].is_active) {
				/* Determine buffer size based on port type */
				buf_size = get_buffer_size_for_port(ports[i].port);

				/* Initialize buffer with zeroes */
				memset(buf, 0, buf_size);

				if (sendto(ports[i].sock, buf, buf_size, 0,
					   (struct sockaddr *)&ports[i].addr,
					   sizeof(struct sockaddr_cif)) < 0) {
					LOG_ERR("Failed to send initial data on port %d, errno %d",
						ports[i].port, errno);
				} else {
					LOG_INF("Sent initial data (%d bytes) on bus %s, port %d",
						buf_size,
						ports[i].bus == CIF_BUS_SLOW ? "SLOW" : "FAST",
						ports[i].port);
				}
			}
		}
	}

	int count = 0;

	/* Start the main loop */
	while (1) {

		/* Check each active port for data */
		for (i = 0; i < MAX_PORTS; i++) {
			if (!ports[i].is_active) {
				continue;
			}

			/* Determine buffer size based on port type */
			buf_size = get_buffer_size_for_port(ports[i].port);

			/* Process data for this port */
			do {
				rc = recvfrom(ports[i].sock, buf, buf_size, MSG_DONTWAIT,
					      (struct sockaddr *)&ports[i].addr, &sl);
				if (rc > 0) {
					/* Process message and get result */
					result = handle_recv_msg(buf, rc, is_master,
								 ports[i].prev_id, ports[i].port);
					ports[i].prev_id = result.prev_id;

					/* Log port activity with status */
					log_port_activity(ports[i].bus, ports[i].port,
							  result.status, buf[0], "RECV");

					/* Send response */
					rc = sendto(ports[i].sock, buf, rc, 0,
						    (struct sockaddr *)&ports[i].addr, sl);
					if (rc < 0) {
						log_port_activity(ports[i].bus, ports[i].port,
								  errno, 0, "SEND_FAIL");
					}
				} else if (rc < 0 && errno != EAGAIN) {
					log_port_activity(ports[i].bus, ports[i].port, errno, 0,
							  "RECV_FAIL");
					break;
				}
			} while (rc > 0);

			/* Handle any errors on this socket */
			handle_extra_errors(ports[i].sock);
		}

		k_sleep(is_master ? K_SECONDS(1) : K_MSEC(10));
		count++;
	}
}

/**
 * @brief Get the appropriate buffer size for a port
 *
 * @param port Port number
 * @return int Buffer size to use
 */
static int get_buffer_size_for_port(int port)
{
	return IS_SYNC_PORT(port) ? SYNC_BUF_SIZE : ASYNC_BUF_SIZE;
}

/**
 * @brief Log port activity with consistent formatting
 *
 * @param bus - Bus number
 * @param port - Port number
 * @param status - Status code
 * @param id - Message ID (if applicable)
 * @param action - Action being performed
 */
static void log_port_activity(int bus, int port, int status, int id, const char *action)
{
	static char status_str[40];

	/* Handle different status types */
	switch (status) {
	case MSG_STATUS_OK:
		snprintf(status_str, sizeof(status_str), "OK");
		LOG_DBG("Port[%s:%d] %s: ID=%d Status=%s", bus == CIF_BUS_SLOW ? "SLOW" : "FAST",
			port, action, id, status_str);
		break;
	case MSG_STATUS_DUP_ID:
		snprintf(status_str, sizeof(status_str), "DUPLICATE_ID");
		LOG_WRN("Port[%s:%d] %s: ID=%d Status=%s", bus == CIF_BUS_SLOW ? "SLOW" : "FAST",
			port, action, id, status_str);
		break;
	case MSG_STATUS_INVALID_DATA:
		snprintf(status_str, sizeof(status_str), "INVALID_DATA");
		LOG_WRN("Port[%s:%d] %s: ID=%d Status=%s", bus == CIF_BUS_SLOW ? "SLOW" : "FAST",
			port, action, id, status_str);
		break;
	case MSG_STATUS_WRONG_SIZE:
		snprintf(status_str, sizeof(status_str), "WRONG_SIZE");
		LOG_WRN("Port[%s:%d] %s: ID=%d Status=%s", bus == CIF_BUS_SLOW ? "SLOW" : "FAST",
			port, action, id, status_str);
		break;
	default:
		/* For error numbers (errno) */
		snprintf(status_str, sizeof(status_str), "ERR=%d", status);
		LOG_ERR("Port[%s:%d] %s: Status=%s", bus == CIF_BUS_SLOW ? "SLOW" : "FAST", port,
			action, status_str);
		break;
	}
}

/**
 * @brief Handle message received from CIF port
 *
 * @param buf - Buffer containing the message
 * @param len - Length of the message
 * @param is_master - Whether this device is in master mode
 * @param prev_id - Previous message ID for comparison
 * @param port - Port number (to determine expected size)
 * @return struct msg_result - Processing result with status and updated prev_id
 */
static struct msg_result handle_recv_msg(char *buf, int len, bool is_master, uint8_t prev_id,
					 int port)
{
	uint8_t curr_id = buf[0];
	struct msg_result result = {prev_id, MSG_STATUS_OK};
	bool data_valid = true;
	int expected_len = get_buffer_size_for_port(port);

	/* case 1: all the bytes in the buffer are the same */
	for (int i = 0; i < len; i++) {
		if (buf[i] != curr_id) {
			data_valid = false;
			break;
		}
	}

	if (!data_valid) {
		result.status = MSG_STATUS_INVALID_DATA;
	}

	/* case 2: the id is not changed against the previous one */
	if (curr_id == prev_id) {
		result.status = MSG_STATUS_DUP_ID;
	}

	/* case 3: the expected length of msg depends on port type */
	if (len != expected_len) {
		result.status = MSG_STATUS_WRONG_SIZE;
	}

	/* Update previous ID */
	result.prev_id = curr_id;

	/* If in slave mode, update buffer with incremented ID */
	if (!is_master) {
		curr_id = (curr_id + 1) % 256;
		for (int i = 0; i < len; i++) {
			buf[i] = curr_id;
		}
	}

	/* eliminate the duplicate ID for SYNC port on slave side */
	if (IS_SYNC_PORT(port) && !is_master) {
		if (result.status == MSG_STATUS_DUP_ID) {
			result.status = MSG_STATUS_OK;
		}
	}

	return result;
}

/**
 * @brief Detect the slot number of the device
 *
 * @return int - slot number if successful, -1 if not
 */
static int slot_detect(void)
{
	int sock = socket(AF_CIF, SOCK_RAW, CIF_RAW_SLAVE);
	int ret;

	if (sock < 0) {
		LOG_ERR("Failed to create CIF socket, errno %d", errno);
		return -1;
	}

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

/**
 * @brief Initialize a CIF socket for a specific mode and bus
 *
 * @param mode - MODE_MASTER or MODE_SLAVE
 * @param bus_type - CIF_BUS_SLOW or CIF_BUS_FAST
 * @return int - socket file descriptor if successful, -1 if not
 */
static int init_cif_socket(int mode, int bus_type)
{
	int sock, ret;

	/* Create socket for the specified mode */
	sock = socket(AF_CIF, SOCK_RAW, mode == MODE_MASTER ? CIF_RAW_MASTER : CIF_RAW_SLAVE);
	if (sock < 0) {
		LOG_ERR("Failed to create CIF socket for bus %d, errno %d", bus_type, errno);
		return -1;
	}

	/* Bind socket to the specified bus */
	struct sockaddr_cif local = {
		.cif_family = AF_CIF,
		.bus = bus_type,
		.slot = -1, /* not used */
		.port = -1, /* not used */
	};

	ret = bind(sock, (struct sockaddr *)&local, sizeof(local));
	if (ret < 0) {
		LOG_ERR("Failed to bind CIF socket for bus %d, errno %d", bus_type, errno);
		close(sock);
		return -1;
	}

	/* Set socket options based on mode */
	if (mode == MODE_MASTER) {
		const struct cif_raw_master_config config = {
			.poll_time = MCB_POLL_TIME,
			.cycle_time = SYNC_CYCLE_TIME,
			.sync_timeout = SYNC_TIMEOUT_TIME,
			.dr = CIF_DR_MPU_P,
			.dt = CIF_DT_MPU_P,
		};
		ret = setsockopt(sock, SOL_CIF_RAW, CIF_OPT_MASTER_CONFIG, &config, sizeof(config));
	} else {
		struct cif_raw_slave_config opt = {
			.want_preempt = 0,
			.dr = CIF_DR_MPU_B,
			.dt = CIF_DT_MPU_B,
		};
		ret = setsockopt(sock, SOL_CIF_RAW, CIF_OPT_SLAVE_CONFIG, &opt, sizeof(opt));
	}

	if (ret < 0) {
		LOG_ERR("Failed to set CIF socket options for bus %d, errno %d", bus_type, errno);
		close(sock);
		return -1;
	}

	return sock;
}

/**
 * @brief Set port activity states based on Kconfig options
 */
static void set_port_activity(void)
{
	/* Default: only the port 16 on slow bus is active */
	ports[PORT_IDX_SLOW_16].is_active = true;

#ifdef CONFIG_SYNC_ECHO
	/* Enable port 1 on slow bus if SYNC_ECHO is enabled */
	ports[PORT_IDX_SLOW_1].is_active = true;
#endif

#ifdef CONFIG_DUAL_BUS
	/* Enable port 16 on fast bus if DUAL_BUS is enabled */
	ports[PORT_IDX_FAST_16].is_active = true;

#ifdef CONFIG_SYNC_ECHO
	/* Enable port 1 on fast bus if both DUAL_BUS and SYNC_ECHO are enabled */
	ports[PORT_IDX_FAST_1].is_active = true;
#endif
#endif
}

/**
 * @brief Initialize ports based on mode (master/slave)
 *
 * @param mode - MODE_MASTER or MODE_SLAVE
 * @return int - 0 if successful, -1 if not
 */
static int init_ports(int mode)
{
	int ret;

	/* Initialize sockets for the buses that are needed */
	int slow_bus_sock = init_cif_socket(mode, CIF_BUS_SLOW);
	int fast_bus_sock = -1;

	if (slow_bus_sock < 0) {
		LOG_ERR("Failed to initialize slow bus socket");
		return -1;
	}

#ifdef CONFIG_DUAL_BUS
	fast_bus_sock = init_cif_socket(mode, CIF_BUS_FAST);
	if (fast_bus_sock < 0) {
		LOG_ERR("Failed to initialize fast bus socket");
		close(slow_bus_sock);
		return -1;
	}
#endif

	/* Assign sockets to ports */
	ports[PORT_IDX_SLOW_16].sock = slow_bus_sock;
	ports[PORT_IDX_SLOW_1].sock = slow_bus_sock;

#ifdef CONFIG_DUAL_BUS
	ports[PORT_IDX_FAST_16].sock = fast_bus_sock;
	ports[PORT_IDX_FAST_1].sock = fast_bus_sock;
#endif

	/* Set port activity states based on Kconfig options */
	set_port_activity();

	/* Configure all active ports */
	for (int i = 0; i < MAX_PORTS; i++) {
		if (!ports[i].is_active) {
			continue;
		}

		struct cif_raw_port_config port_cfg = {
			.port = ports[i].port,
			.flags = CIF_PORT_FLG_ENABLE,
		};

		/* Additional configuration based on mode */
		if (mode == MODE_MASTER) {
			port_cfg.slot = 3; /* Target slot is assumed to be 3 */
			port_cfg.async_interval = ASYNC_INTERVAL_TIME;
			port_cfg.async_timeout = ASYNC_TIMEOUT_TIME;
			port_cfg.async_bandwidth = ASYNC_DEFAULT_BANDWIDTH;
		} else {
			port_cfg.flags |= CIF_PORT_FLG_ALLOW_WRITE;
			/* Use appropriate buffer size based on port type */
			port_cfg.slave_max_recv_len =
				get_buffer_size_for_port(ports[i].port) + 100; /* Add margin */
		}

		ret = setsockopt(ports[i].sock, SOL_CIF_RAW, CIF_OPT_PORT, &port_cfg,
				 sizeof(port_cfg));
		if (ret < 0) {
			LOG_ERR("Failed to set port options for bus %d, port %d, errno %d",
				ports[i].bus, ports[i].port, errno);
			ports[i].is_active = false;
			continue;
		}

		/* Set up the address for this port */
		ports[i].addr.cif_family = AF_CIF;
		ports[i].addr.port = ports[i].port;

		if (mode == MODE_MASTER) {
			ports[i].addr.slot = 3; /* Target slot is assumed to be 3 */
			ports[i].slot = 3;
		}

		LOG_INF("Successfully configured %s port %d on bus %d (buffer size: %d)",
			mode == MODE_MASTER ? "master" : "slave", ports[i].port, ports[i].bus,
			get_buffer_size_for_port(ports[i].port));
	}

	return 0;
}

/**
 * @brief Handle extra error conditions from CIF socket
 *
 * @param sock - Socket to check for errors
 * @return int - 0 if successful
 */
static int handle_extra_errors(int sock)
{
	int ret;
	struct cif_error_filter err_mask = {
		.flags = 0,
	};
	socklen_t err_len = sizeof(err_mask);

	/* Get error mask */
	ret = getsockopt(sock, SOL_CIF_RAW, CIF_OPT_ERROR, &err_mask, &err_len);
	if (ret < 0) {
		if (errno != ENOENT) {
			LOG_WRN("Failed to get error mask, errno %d", errno);
		}
		return 0; /* No errors or no active connections */
	}

	/* Clear error mask */
	ret = setsockopt(sock, SOL_CIF_RAW, CIF_OPT_ERROR, &err_mask, sizeof(err_mask));
	if (ret < 0) {
		LOG_WRN("Failed to clear error mask, errno %d", errno);
	}

	/* Handle the specific error conditions if any */
	if (err_mask.error_mask) {
		if (err_mask.error_mask & (CIF_ERR_PREV_PREEMPT | CIF_ERR_PREEMPT)) {
			LOG_WRN("Preempt error detected, switch to slave mode");
			/* Close socket will release all resources */
		} else if (err_mask.error_mask & (CIF_ERR_R_ERROR | CIF_ERR_I_ERROR)) {
			/* Handle I_ERROR/R_ERROR by getting additional info */
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
