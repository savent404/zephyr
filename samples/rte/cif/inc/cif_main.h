/**
 * @file main.h
 * @author Liao YuanKai (savent_gate@outlook.com)
 * @brief
 * @version 0.1
 * @date 2025-01-28
 *
 * @copyright Copyright (c) 2025 SYSTech Co.
 *
 */
#pragma once

#include <zephyr/net/socketcif.h>

#define MAX_DEV 2

#define MCB_POLL_TIME           (655 * 1000)                      /* 655us */
#define SYNC_CYCLE_TIME         CONFIG_CIF_SYNC_CYCLE_TIME_US     /* us */
#define SYNC_TIMEOUT_TIME       (SYNC_CYCLE_TIME * 5)             /* 5 cycles */
#define ASYNC_INTERVAL_TIME     CONFIG_CIF_ASYNC_INTERVAL_TIME_US /* us */
#define ASYNC_TIMEOUT_TIME      (200 * 1000)                      /* 200ms */
#define ASYNC_DEFAULT_BANDWIDTH 0                                 /* no limitation */

#if CONFIG_MCB_SYSTECH_HW_WORKAROUND
#define PORT_ID_DISC 0x00
#endif
#define PORT_ID_CFG  0x08
#define PORT_ID_IO   0x01
#define PORT_ID_ETH0 0x10
#define PORT_ID_ETH1 0x11
#define PORT_ID_ETH2 0x12
#define PORT_ID_ETH3 0x13

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

struct context {
	int target_sid;
	int target_port;
	uint32_t target_duration; /* unit: ms */
	uint32_t target_pps;
	enum {
		role_master,
		role_slave,
	} target_role;
	enum {
		bus_low,
		bus_high,
	} target_bus;
	enum {
		normal,
		preempt,
	} target_opt;

#define CMD_NONE      0
#define CMD_DISCOVERY 1
#define CMD_CONFIG    2
#define CMD_IO        3
#define CMD_SWITCH    4
#define CMD_OPEN      5
#define CMD_CLOSE     6
#define CMD_PERF      7
#define CMD_LIST      8
#define CMD_STATS     9
	uint32_t cmd;

#define STATE_IDLE     0
#define STATE_DISCOVER 1
#define STATE_CONFIG   2
#define STATE_IO_START 3
#define STATE_IO       4
#define STATE_OPEN     5
#define STATE_CLOSE    6
#define STATE_STATS    7
	uint32_t state;

	uint32_t stat_ok;
	uint32_t stat_failed;

	int64_t systick_begin;
	int64_t systick_end;

	struct sockaddr_cif curr;
	socklen_t curr_len;

	struct k_sem terminate_sem;

	/* For open command */
	uint8_t initial_data[64];
	size_t initial_data_len;
	bool check_response;

	bool perf_mode;
};

int slave_start(void);
int slave_cancel(void);
int master_start(void);
int master_cancel(void);

/**
 * @brief This function is used to get and handle extra errors.
 * @return 1 if Preempt event detected. (For master mode, it shall switch to slave mode)
 * @return 0 if no extra errors detected.
 */
int handle_extra_errors(int sock);

#define MAX_OPEN_PORTS  16
#define MAX_OPEN_ERRORS 32
struct open_port_s {
	uint8_t sid;
	uint8_t port;
	bool active;
	uint32_t stat_sent;
	uint32_t stat_received;
	uint32_t stat_error;
	uint32_t open_timestamp;

	uint32_t prev_stat_sent;
	uint32_t prev_stat_received;
	uint32_t prev_stat_error;
	uint32_t prev_timestamp;
	uint32_t issue3_last_tx_us;
	bool issue3_waiting_rx;
	uint32_t lag_rx_avg;
	uint32_t lag_rx_max;
	uint32_t lag_tx_avg;
	uint32_t lag_tx_max;
};

extern struct open_port_s open_ports[MAX_OPEN_PORTS];
