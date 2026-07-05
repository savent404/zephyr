/**
 * @license This project is CLOSED SOURCE, All Rights Reserved
 * Copyright (c) 2025 SYSTech Co.
 */
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/socketcif.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include <string.h>

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

#define CIF_FAULT_PORT_COUNT 32
#define CIF_FAULT_PORT_ALL   0xff

enum fault_mode {
	FAULT_OFF = 0,
	FAULT_LOW,
	FAULT_HIGH,
};

struct slave_fault_cfg {
	bool enabled;
	enum fault_mode mode;
	uint8_t selector;
	uint32_t recv_us;
	uint32_t send_us;
	uint32_t every_n;
	uint32_t rx_seen[CIF_FAULT_PORT_COUNT];
	uint32_t tx_seen[CIF_FAULT_PORT_COUNT];
	uint32_t rx_injected[CIF_FAULT_PORT_COUNT];
	uint32_t tx_injected[CIF_FAULT_PORT_COUNT];
	struct k_spinlock lock;
};

#if CONFIG_CIF_SAMPLE_FAULT_INJECT
static struct slave_fault_cfg slave_fault;

static const uint8_t fault_ports[] = {
	PORT_ID_CFG, PORT_ID_IO, PORT_ID_ETH0, PORT_ID_ETH1, PORT_ID_ETH2, PORT_ID_ETH3,
};

static const char *fault_mode_name(enum fault_mode mode)
{
	switch (mode) {
	case FAULT_LOW:
		return "low";
	case FAULT_HIGH:
		return "high";
	case FAULT_OFF:
	default:
		return "off";
	}
}

static const char *fault_port_name(uint8_t port)
{
	switch (port) {
	case PORT_ID_CFG:
		return "cfg";
	case PORT_ID_IO:
		return "io";
	case PORT_ID_ETH0:
		return "eth0";
	case PORT_ID_ETH1:
		return "eth1";
	case PORT_ID_ETH2:
		return "eth2";
	case PORT_ID_ETH3:
		return "eth3";
	case CIF_FAULT_PORT_ALL:
		return "all";
	default:
		return "unknown";
	}
}

static bool fault_parse_port(const char *name, uint8_t *port)
{
	struct {
		const char *name;
		uint8_t port;
	} map[] = {
		{"cfg", PORT_ID_CFG},        {"io", PORT_ID_IO},     {"eth0", PORT_ID_ETH0},
		{"eth1", PORT_ID_ETH1},      {"eth2", PORT_ID_ETH2}, {"eth3", PORT_ID_ETH3},
		{"all", CIF_FAULT_PORT_ALL},
	};

	for (size_t i = 0; i < ARRAY_SIZE(map); ++i) {
		if (strcmp(name, map[i].name) == 0) {
			*port = map[i].port;
			return true;
		}
	}

	return false;
}

static bool fault_port_matches(uint8_t selector, uint8_t port)
{
	return selector == CIF_FAULT_PORT_ALL || selector == port;
}

static uint32_t fault_claim_delay(uint8_t port, bool is_tx)
{
	uint32_t delay_us = 0;
	k_spinlock_key_t key = k_spin_lock(&slave_fault.lock);
	uint32_t *seen = is_tx ? slave_fault.tx_seen : slave_fault.rx_seen;
	uint32_t *injected = is_tx ? slave_fault.tx_injected : slave_fault.rx_injected;

	if (!slave_fault.enabled || port >= CIF_FAULT_PORT_COUNT ||
	    !fault_port_matches(slave_fault.selector, port)) {
		k_spin_unlock(&slave_fault.lock, key);
		return 0;
	}

	seen[port]++;
	if (slave_fault.mode == FAULT_HIGH ||
	    (slave_fault.mode == FAULT_LOW && slave_fault.every_n != 0 &&
	     (seen[port] % slave_fault.every_n) == 0U)) {
		injected[port]++;
		delay_us = is_tx ? slave_fault.send_us : slave_fault.recv_us;
	}

	k_spin_unlock(&slave_fault.lock, key);
	return delay_us;
}

static uint32_t fault_delay_before_rx(uint8_t port)
{
	return fault_claim_delay(port, false);
}

static uint32_t fault_delay_before_tx(uint8_t port)
{
	return fault_claim_delay(port, true);
}

static void fault_sleep_if_needed(uint32_t delay_us)
{
	if (delay_us > 0U) {
		k_usleep(delay_us);
	}
}

static int cif_fault_cmd(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print(sh, "usage: cif_fault show|off|set <low|high> "
				"<cfg|io|eth0|eth1|eth2|eth3|all> <recv_us> <send_us> [every_n]");
		return -EINVAL;
	}

	if (strcmp(argv[1], "show") == 0) {
		bool enabled;
		enum fault_mode mode;
		uint8_t selector;
		uint32_t recv_us;
		uint32_t send_us;
		uint32_t every_n;
		uint32_t rx_seen[CIF_FAULT_PORT_COUNT];
		uint32_t tx_seen[CIF_FAULT_PORT_COUNT];
		uint32_t rx_injected[CIF_FAULT_PORT_COUNT];
		uint32_t tx_injected[CIF_FAULT_PORT_COUNT];
		k_spinlock_key_t key = k_spin_lock(&slave_fault.lock);

		enabled = slave_fault.enabled;
		mode = slave_fault.mode;
		selector = slave_fault.selector;
		recv_us = slave_fault.recv_us;
		send_us = slave_fault.send_us;
		every_n = slave_fault.every_n;
		memcpy(rx_seen, slave_fault.rx_seen, sizeof(rx_seen));
		memcpy(tx_seen, slave_fault.tx_seen, sizeof(tx_seen));
		memcpy(rx_injected, slave_fault.rx_injected, sizeof(rx_injected));
		memcpy(tx_injected, slave_fault.tx_injected, sizeof(tx_injected));
		k_spin_unlock(&slave_fault.lock, key);

		shell_print(sh, "enabled=%d mode=%s selector=%s recv_us=%u send_us=%u every_n=%u",
			    enabled, fault_mode_name(mode), fault_port_name(selector), recv_us,
			    send_us, every_n);
		for (size_t i = 0; i < ARRAY_SIZE(fault_ports); ++i) {
			uint8_t port = fault_ports[i];

			shell_print(sh, "%s rx_seen=%u rx_injected=%u tx_seen=%u tx_injected=%u",
				    fault_port_name(port), rx_seen[port], rx_injected[port],
				    tx_seen[port], tx_injected[port]);
		}
		return 0;
	}

	if (strcmp(argv[1], "off") == 0) {
		k_spinlock_key_t key = k_spin_lock(&slave_fault.lock);

		slave_fault.enabled = false;
		slave_fault.mode = FAULT_OFF;
		slave_fault.selector = CIF_FAULT_PORT_ALL;
		slave_fault.recv_us = 0;
		slave_fault.send_us = 0;
		slave_fault.every_n = 0;
		k_spin_unlock(&slave_fault.lock, key);
		shell_print(sh, "fault injection disabled");
		return 0;
	}

	if (strcmp(argv[1], "set") == 0) {
		enum fault_mode mode;
		uint8_t selector;
		uint32_t recv_us;
		uint32_t send_us;
		uint32_t every_n;

		if (argc < 6 || argc > 7) {
			shell_print(
				sh,
				"usage: cif_fault set <low|high> <cfg|io|eth0|eth1|eth2|eth3|all> "
				"<recv_us> <send_us> [every_n]");
			return -EINVAL;
		}

		if (strcmp(argv[2], "low") == 0) {
			mode = FAULT_LOW;
		} else if (strcmp(argv[2], "high") == 0) {
			mode = FAULT_HIGH;
		} else {
			shell_print(sh, "invalid mode: %s", argv[2]);
			return -EINVAL;
		}

		if (!fault_parse_port(argv[3], &selector)) {
			shell_print(sh, "invalid port selector: %s", argv[3]);
			return -EINVAL;
		}

		recv_us = (uint32_t)strtoul(argv[4], NULL, 0);
		send_us = (uint32_t)strtoul(argv[5], NULL, 0);
		every_n = (argc == 7) ? (uint32_t)strtoul(argv[6], NULL, 0) : 8U;
		if (mode == FAULT_HIGH) {
			every_n = 1U;
		} else if (every_n == 0U) {
			shell_print(sh, "every_n must be > 0 for low mode");
			return -EINVAL;
		}

		k_spinlock_key_t key = k_spin_lock(&slave_fault.lock);

		slave_fault.enabled = true;
		slave_fault.mode = mode;
		slave_fault.selector = selector;
		slave_fault.recv_us = recv_us;
		slave_fault.send_us = send_us;
		slave_fault.every_n = every_n;
		memset(slave_fault.rx_seen, 0, sizeof(slave_fault.rx_seen));
		memset(slave_fault.tx_seen, 0, sizeof(slave_fault.tx_seen));
		memset(slave_fault.rx_injected, 0, sizeof(slave_fault.rx_injected));
		memset(slave_fault.tx_injected, 0, sizeof(slave_fault.tx_injected));
		k_spin_unlock(&slave_fault.lock, key);

		shell_print(sh,
			    "fault injection enabled: mode=%s selector=%s recv_us=%u send_us=%u "
			    "every_n=%u",
			    fault_mode_name(mode), fault_port_name(selector), recv_us, send_us,
			    every_n);
		return 0;
	}

	shell_print(sh, "unknown subcommand: %s", argv[1]);
	return -EINVAL;
}

SHELL_CMD_REGISTER(cif_fault, NULL, "CIF sample fault injection", cif_fault_cmd);
#else
static uint32_t fault_delay_before_rx(uint8_t port)
{
	ARG_UNUSED(port);
	return 0;
}

static uint32_t fault_delay_before_tx(uint8_t port)
{
	ARG_UNUSED(port);
	return 0;
}

static void fault_sleep_if_needed(uint32_t delay_us)
{
	ARG_UNUSED(delay_us);
}
#endif

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
		fault_sleep_if_needed(fault_delay_before_rx(port));
		ret = recvfrom(sock, rx_buf, sizeof(rx_buf), 0, (struct sockaddr *)&port_addr, &sl);
		if (ret < 0 && errno != EAGAIN) {
			LOG_ERR("Failed to receive data, errno %d", errno);
		} else if (ret > 0) {
			LOG_INF("Received data from port %d", port);
			LOG_HEXDUMP_INF(rx_buf, ret, "Data:");

#if !CONFIG_CIF_SLAVE_ONLY_RECV
			fault_sleep_if_needed(fault_delay_before_tx(port));
			ret = sendto(sock, rx_buf, ret, 0, (struct sockaddr *)&port_addr, sl);
			if (ret < 0 && errno != EAGAIN) {
				LOG_ERR("Failed to send data back, errno %d", errno);
			}
#endif

			something2do = true;
		}
	} else {
		/* performance mode, send any data to the port */
		fault_sleep_if_needed(fault_delay_before_rx(port));
		ret = recvfrom(sock, rx_buf, sizeof(rx_buf), 0, (struct sockaddr *)&port_addr, &sl);

		if (ret < 0 && errno != EAGAIN) {
			LOG_ERR("Failed to receive data, errno %d", errno);
		} else if (ret > 0) {
			LOG_INF("Received data from port %d", port);
			LOG_HEXDUMP_INF(rx_buf, ret, "Data:");
			something2do = true;
		}

#if !CONFIG_CIF_SLAVE_ONLY_RECV
		fault_sleep_if_needed(fault_delay_before_tx(port));
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
		struct cif_ready_map ready = {};
		socklen_t ready_len = sizeof(ready);

		ret = getsockopt(sock, SOL_CIF_RAW, CIF_OPT_PORT_RDY_MSK, &ready, &ready_len);
		if (ret < 0) {
			LOG_ERR("Failed to query ready ports, errno %d", errno);
		}

		if (ready.ready_mask == 0) {
			k_usleep(10);
			if (k_sem_take(&terminate_sem, K_NO_WAIT) == 0) {
				zsock_close(sock);
				break;
			}
			continue;
		}

		socklen_t sl = sizeof(port_cfg);
		bool new_config = false;

		if (ready.ready_mask & BIT(PORT_ID_CFG)) {
			fault_sleep_if_needed(fault_delay_before_rx(PORT_ID_CFG));
			ret = recvfrom(sock, REG_BUF_MODIFY(config), REG_BUF_LEN(config), 0,
				       (struct sockaddr *)&port_cfg, &sl);

			if (ret >= 0) {
				LOG_INF("Received config data");
				new_config = ret > 0 ? true : false;
			} else if (errno != EAGAIN) {
				LOG_ERR("Failed to receive config data, errno %d", errno);
			}
		}

		if (new_config) {
			memcpy(REG_BUF_REG(config), REG_BUF_MODIFY(config), REG_BUF_LEN(config));
			fault_sleep_if_needed(fault_delay_before_tx(PORT_ID_CFG));
			ret = sendto(sock, REG_BUF_REG(config), REG_BUF_LEN(config), 0,
				     (struct sockaddr *)&port_cfg, sizeof(port_cfg));
			if (ret >= 0) {
				LOG_INF("Sent config data back");
			} else if (errno != EAGAIN) {
				LOG_ERR("Failed to send config data, errno %d", errno);
			}
		}

		bool new_io = false;

		sl = sizeof(port_io);
		if (ready.ready_mask & BIT(PORT_ID_IO)) {
			fault_sleep_if_needed(fault_delay_before_rx(PORT_ID_IO));
			ret = recvfrom(sock, REG_BUF_MODIFY(io), REG_BUF_LEN(io), 0,
				       (struct sockaddr *)&port_io, &sl);
			if (ret > 0) {
				LOG_INF("Received io data");
				memcpy(REG_BUF_REG(io), REG_BUF_MODIFY(io), REG_BUF_LEN(io));
				new_io = true;
			} else if (ret < 0 && errno != EAGAIN) {
				LOG_ERR("Failed to receive io data, errno %d", errno);
			}
		}

		REG_BUF_REG(io)[5]++;
		fault_sleep_if_needed(fault_delay_before_tx(PORT_ID_IO));
		ret = sendto(sock, REG_BUF_REG(io), REG_BUF_LEN(io), 0, (struct sockaddr *)&port_io,
			     sizeof(port_io));
		if (ret < 0) {
			LOG_INF("Failed to send io data, errno %d", errno);
		}

		int no_job = 0;

		if (ready.ready_mask & BIT(PORT_ID_ETH0)) {
			no_job += deal_ethernet_data(sock, PORT_ID_ETH0);
		}
		if (ready.ready_mask & BIT(PORT_ID_ETH1)) {
			no_job += deal_ethernet_data(sock, PORT_ID_ETH1);
		}
		if (ready.ready_mask & BIT(PORT_ID_ETH2)) {
			no_job += deal_ethernet_data(sock, PORT_ID_ETH2);
		}
		if (ready.ready_mask & BIT(PORT_ID_ETH3)) {
			no_job += deal_ethernet_data(sock, PORT_ID_ETH3);
		}
		no_job += handle_extra_errors(sock);

		if (no_job == 0 && !new_config && !new_io) {
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
