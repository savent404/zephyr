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

#define MCB_POLL_TIME           (655 * 1000) /* 655us */
#define SYNC_CYCLE_TIME         2000         /* 2ms */
#define SYNC_TIMEOUT_TIME       10000        /* 10ms (5 times of SYNC_CYCLE_TIME) */
#define ASYNC_INTERVAL_TIME     10000        /* 10ms */
#define ASYNC_TIMEOUT_TIME      100000       /* 100ms */
#define ASYNC_DEFAULT_BANDWIDTH 0            /* no limitation */

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
	uint32_t cmd;

#define STATE_IDLE     0
#define STATE_DISCOVER 1
#define STATE_CONFIG   2
#define STATE_IO_START 3
#define STATE_IO       4
#define STATE_OPEN     5
#define STATE_CLOSE    6
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
};

int slave_start(void);
int slave_cancel(void);
int master_start(void);
int master_cancel(void);
