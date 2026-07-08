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
	uint64_t fault_delay_due_us[2][CIF_FAULT_PORT_COUNT];
	bool fault_delay_pending[2][CIF_FAULT_PORT_COUNT];
	struct k_spinlock lock;
};

#if CONFIG_CIF_SAMPLE_FAULT_INJECT
static struct slave_fault_cfg slave_fault;

enum fault_delay_dir {
	FAULT_DELAY_RX = 0,
	FAULT_DELAY_TX = 1,
};

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

static uint64_t fault_now_us(void)
{
	return k_cyc_to_us_floor64(k_cycle_get_64());
}

static bool fault_deadline_reached(uint64_t now_us, uint64_t due_us)
{
	return now_us >= due_us;
}

static bool fault_delay_ready(uint8_t port, bool is_tx)
{
	uint32_t delay_us = 0;
	uint64_t now_us = fault_now_us();
	enum fault_delay_dir dir = is_tx ? FAULT_DELAY_TX : FAULT_DELAY_RX;
	k_spinlock_key_t key = k_spin_lock(&slave_fault.lock);
	uint32_t *seen = is_tx ? slave_fault.tx_seen : slave_fault.rx_seen;
	uint32_t *injected = is_tx ? slave_fault.tx_injected : slave_fault.rx_injected;
	uint64_t *due_us = slave_fault.fault_delay_due_us[dir];
	bool *pending = slave_fault.fault_delay_pending[dir];

	if (!slave_fault.enabled || port >= CIF_FAULT_PORT_COUNT ||
	    !fault_port_matches(slave_fault.selector, port)) {
		k_spin_unlock(&slave_fault.lock, key);
		return true;
	}

	if (pending[port]) {
		if (!fault_deadline_reached(now_us, due_us[port])) {
			k_spin_unlock(&slave_fault.lock, key);
			return false;
		}
		pending[port] = false;
		k_spin_unlock(&slave_fault.lock, key);
		return true;
	}

	seen[port]++;
	if (slave_fault.mode == FAULT_HIGH ||
	    (slave_fault.mode == FAULT_LOW && slave_fault.every_n != 0 &&
	     (seen[port] % slave_fault.every_n) == 0U)) {
		injected[port]++;
		delay_us = is_tx ? slave_fault.send_us : slave_fault.recv_us;
	}

	if (delay_us > 0U) {
		due_us[port] = now_us + delay_us;
		pending[port] = true;
		k_spin_unlock(&slave_fault.lock, key);
		return false;
	}

	k_spin_unlock(&slave_fault.lock, key);
	return true;
}

static void fault_delay_clear_pending(void)
{
	k_spinlock_key_t key = k_spin_lock(&slave_fault.lock);

	memset(slave_fault.fault_delay_due_us, 0, sizeof(slave_fault.fault_delay_due_us));
	memset(slave_fault.fault_delay_pending, 0, sizeof(slave_fault.fault_delay_pending));
	k_spin_unlock(&slave_fault.lock, key);
}

static bool fault_delay_pending_for(uint8_t port, bool is_tx)
{
	if (port >= CIF_FAULT_PORT_COUNT) {
		return false;
	}

	enum fault_delay_dir dir = is_tx ? FAULT_DELAY_TX : FAULT_DELAY_RX;
	k_spinlock_key_t key = k_spin_lock(&slave_fault.lock);
	bool pending = slave_fault.enabled && slave_fault.fault_delay_pending[dir][port];

	k_spin_unlock(&slave_fault.lock, key);
	return pending;
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
		memset(slave_fault.fault_delay_due_us, 0, sizeof(slave_fault.fault_delay_due_us));
		memset(slave_fault.fault_delay_pending, 0, sizeof(slave_fault.fault_delay_pending));
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
		memset(slave_fault.fault_delay_due_us, 0, sizeof(slave_fault.fault_delay_due_us));
		memset(slave_fault.fault_delay_pending, 0, sizeof(slave_fault.fault_delay_pending));
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
static bool fault_delay_ready(uint8_t port, bool is_tx)
{
	ARG_UNUSED(port);
	ARG_UNUSED(is_tx);
	return true;
}

static bool fault_delay_pending_for(uint8_t port, bool is_tx)
{
	ARG_UNUSED(port);
	ARG_UNUSED(is_tx);
	return false;
}

static void fault_delay_clear_pending(void)
{
}
#endif

#define CIF_REG_TX_PENDING_MTU 128

struct reg_tx_pending_state {
	bool active;
	bool delay_elapsed;
	uint16_t len;
	uint8_t buf[CIF_REG_TX_PENDING_MTU];
};

static struct reg_tx_pending_state reg_tx_pending[CIF_FAULT_PORT_COUNT];

static bool queue_reg_tx(uint8_t port, const uint8_t *buf, uint16_t len, bool delay_elapsed)
{
	if (port >= CIF_FAULT_PORT_COUNT) {
		return false;
	}

	if (len > CIF_REG_TX_PENDING_MTU) {
		len = CIF_REG_TX_PENDING_MTU;
	}

	memcpy(reg_tx_pending[port].buf, buf, len);
	reg_tx_pending[port].len = len;
	reg_tx_pending[port].delay_elapsed = delay_elapsed;
	reg_tx_pending[port].active = true;
	return true;
}

static bool reg_tx_pending_for(uint8_t port)
{
	return port < CIF_FAULT_PORT_COUNT && reg_tx_pending[port].active;
}

static int flush_tx(int sock, uint8_t port, const struct sockaddr_cif *port_addr, socklen_t sl)
{
	int ret;

	if (!reg_tx_pending_for(port)) {
		return 0;
	}

	if (!reg_tx_pending[port].delay_elapsed) {
		if (!fault_delay_ready(port, true)) {
			return 0;
		}
		reg_tx_pending[port].delay_elapsed = true;
	}

	ret = sendto(sock, reg_tx_pending[port].buf, reg_tx_pending[port].len, 0,
		     (const struct sockaddr *)port_addr, sl);
	if (ret >= 0) {
		reg_tx_pending[port].active = false;
		reg_tx_pending[port].delay_elapsed = false;
		return 2;
	}

	if (errno != EAGAIN) {
		reg_tx_pending[port].active = false;
		reg_tx_pending[port].delay_elapsed = false;
		LOG_ERR("Failed to send data back on port %d, errno %d", port, errno);
	}
	return 0;
}

static int send_tx(int sock, uint8_t port, const uint8_t *buf, uint16_t len,
		   const struct sockaddr_cif *port_addr, socklen_t sl)
{
	int ret;

	if (!fault_delay_ready(port, true)) {
		return queue_reg_tx(port, buf, len, false) ? 1 : 0;
	}

	ret = sendto(sock, buf, len, 0, (const struct sockaddr *)port_addr, sl);
	if (ret >= 0) {
		return 2;
	}

	if (errno == EAGAIN) {
		return queue_reg_tx(port, buf, len, true) ? 1 : 0;
	}

	LOG_ERR("Failed to send data back on port %d, errno %d", port, errno);
	return 0;
}

static bool port_rx_needs_service(uint32_t ready_mask, uint8_t port)
{
	return (ready_mask & BIT(port)) || fault_delay_pending_for(port, false);
}

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

struct eth_tx_pending_state {
	bool active;
	bool delay_elapsed;
	uint16_t len;
	uint8_t buf[CIF_ASYNC_MTU];
};

static struct eth_tx_pending_state eth_tx_pending[CIF_FAULT_PORT_COUNT];

static void reset_slave_pending_state(void)
{
	memset(reg_tx_pending, 0, sizeof(reg_tx_pending));
	memset(eth_tx_pending, 0, sizeof(eth_tx_pending));
	fault_delay_clear_pending();
}

static bool queue_eth_tx(uint8_t port, const uint8_t *buf, uint16_t len, bool delay_elapsed)
{
	if (port >= CIF_FAULT_PORT_COUNT) {
		return false;
	}

	if (len > CIF_ASYNC_MTU) {
		len = CIF_ASYNC_MTU;
	}

	memcpy(eth_tx_pending[port].buf, buf, len);
	eth_tx_pending[port].len = len;
	eth_tx_pending[port].delay_elapsed = delay_elapsed;
	eth_tx_pending[port].active = true;
	return true;
}

static int flush_pending_eth_tx(int sock, uint8_t port, const struct sockaddr_cif *port_addr,
				socklen_t sl)
{
	int ret;

	if (port >= CIF_FAULT_PORT_COUNT || !eth_tx_pending[port].active) {
		return 0;
	}

	if (!eth_tx_pending[port].delay_elapsed) {
		if (!fault_delay_ready(port, true)) {
			return 0;
		}
		eth_tx_pending[port].delay_elapsed = true;
	}

	ret = sendto(sock, eth_tx_pending[port].buf, eth_tx_pending[port].len, 0,
		     (const struct sockaddr *)port_addr, sl);
	if (ret >= 0) {
		eth_tx_pending[port].active = false;
		eth_tx_pending[port].delay_elapsed = false;
		LOG_INF("Sent data back to port %d", port);
		return 1;
	}

	if (errno != EAGAIN) {
		eth_tx_pending[port].active = false;
		eth_tx_pending[port].delay_elapsed = false;
		LOG_ERR("Failed to send data back, errno %d", errno);
	}
	return 0;
}

static bool eth_port_needs_service(uint32_t ready_mask, uint8_t port)
{
	return (ready_mask & BIT(port)) ||
	       (port < CIF_FAULT_PORT_COUNT &&
		(eth_tx_pending[port].active || fault_delay_pending_for(port, false)));
}

static bool eth_ports_need_service(uint32_t ready_mask)
{
	return eth_port_needs_service(ready_mask, PORT_ID_ETH0) ||
	       eth_port_needs_service(ready_mask, PORT_ID_ETH1) ||
	       eth_port_needs_service(ready_mask, PORT_ID_ETH2) ||
	       eth_port_needs_service(ready_mask, PORT_ID_ETH3);
}

static bool slave_has_pending_work(uint32_t ready_mask)
{
	return port_rx_needs_service(ready_mask, PORT_ID_CFG) ||
	       port_rx_needs_service(ready_mask, PORT_ID_IO) || reg_tx_pending_for(PORT_ID_CFG) ||
	       reg_tx_pending_for(PORT_ID_IO) || eth_ports_need_service(ready_mask);
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

	if (port < CIF_FAULT_PORT_COUNT && eth_tx_pending[port].active) {
		return flush_pending_eth_tx(sock, port, &port_addr, sl);
	}

	if (!ctx_.perf_mode) {
		/* don't care about the performance, using echo to validate loopback */
		if (!fault_delay_ready(port, false)) {
			return 0;
		}
		ret = recvfrom(sock, rx_buf, sizeof(rx_buf), 0, (struct sockaddr *)&port_addr, &sl);
		if (ret < 0 && errno != EAGAIN) {
			LOG_ERR("Failed to receive data, errno %d", errno);
		} else if (ret > 0) {
			LOG_INF("Received data from port %d", port);
			LOG_HEXDUMP_INF(rx_buf, ret, "Data:");

#if !CONFIG_CIF_SLAVE_ONLY_RECV
			uint16_t tx_len = ret;

			if (!fault_delay_ready(port, true)) {
				queue_eth_tx(port, rx_buf, tx_len, false);
			} else {
				ret = sendto(sock, rx_buf, tx_len, 0, (struct sockaddr *)&port_addr,
					     sl);
				if (ret < 0 && errno == EAGAIN) {
					queue_eth_tx(port, rx_buf, tx_len, true);
				} else if (ret < 0) {
					LOG_ERR("Failed to send data back, errno %d", errno);
				}
			}
#endif

			something2do = true;
		}
	} else {
		/* performance mode, send any data to the port */
		if (!fault_delay_ready(port, false)) {
			return 0;
		}
		ret = recvfrom(sock, rx_buf, sizeof(rx_buf), 0, (struct sockaddr *)&port_addr, &sl);

		if (ret < 0 && errno != EAGAIN) {
			LOG_ERR("Failed to receive data, errno %d", errno);
		} else if (ret > 0) {
			LOG_INF("Received data from port %d", port);
			LOG_HEXDUMP_INF(rx_buf, ret, "Data:");
			something2do = true;
		}

#if !CONFIG_CIF_SLAVE_ONLY_RECV
		if (!fault_delay_ready(port, true)) {
			queue_eth_tx(port, rx_buf, 1500, false);
			something2do = true;
		} else {
			ret = sendto(sock, rx_buf, 1500, 0, (struct sockaddr *)&port_addr, sl);
			if (ret < 0) {
				if (errno == EAGAIN) {
					something2do = queue_eth_tx(port, rx_buf, 1500, true);
				} else {
					LOG_ERR("Failed to send data back, errno %d", errno);
				}
			} else if (ret > 0) {
				LOG_INF("Sent data back to port %d", port);
				something2do = true;
			}
		}
#endif
	}

	return something2do ? 1 : 0;
}

static int service_ethernet_ports(int sock, uint32_t ready_mask)
{
	int has_job = 0;

	if (eth_port_needs_service(ready_mask, PORT_ID_ETH0)) {
		has_job += deal_ethernet_data(sock, PORT_ID_ETH0);
	}
	if (eth_port_needs_service(ready_mask, PORT_ID_ETH1)) {
		has_job += deal_ethernet_data(sock, PORT_ID_ETH1);
	}
	if (eth_port_needs_service(ready_mask, PORT_ID_ETH2)) {
		has_job += deal_ethernet_data(sock, PORT_ID_ETH2);
	}
	if (eth_port_needs_service(ready_mask, PORT_ID_ETH3)) {
		has_job += deal_ethernet_data(sock, PORT_ID_ETH3);
	}

	return has_job;
}

static int slave_task(void)
{
	LOG_INF("Running in slave mode");
	reset_slave_pending_state();

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
		uint32_t ready_mask = 0;
		socklen_t ready_len = sizeof(ready_mask);

		ret = getsockopt(sock, SOL_CIF_RAW, CIF_OPT_PORT_RDY_MSK, &ready_mask, &ready_len);
		if (ret < 0) {
			LOG_ERR("Failed to query ready ports, errno %d", errno);
		}

		if (ready_mask == 0 && !slave_has_pending_work(ready_mask)) {
			if (k_sem_take(&terminate_sem, K_USEC(10)) == 0) {
				zsock_close(sock);
				break;
			}
			handle_extra_errors(sock);
			continue;
		}

		int has_job = 0;
		socklen_t sl = sizeof(port_cfg);
		bool new_config = false;
		bool config_tx_pending = reg_tx_pending_for(PORT_ID_CFG);
		int tx_res;

		tx_res = flush_tx(sock, PORT_ID_CFG, &port_cfg, sizeof(port_cfg));
		has_job += tx_res > 0 ? 1 : 0;

		if (!config_tx_pending && port_rx_needs_service(ready_mask, PORT_ID_CFG) &&
		    fault_delay_ready(PORT_ID_CFG, false)) {
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
			uint8_t cfg_port = PORT_ID_CFG;
			uint8_t *cfg_tx = REG_BUF_REG(config);
			uint8_t *cfg_mod = REG_BUF_MODIFY(config);
			uint16_t cfg_len = REG_BUF_LEN(config);

			memcpy(cfg_tx, cfg_mod, cfg_len);
			tx_res = send_tx(sock, cfg_port, cfg_tx, cfg_len, &port_cfg,
					 sizeof(port_cfg));
			has_job += tx_res > 0 ? 1 : 0;
			if (tx_res == 2) {
				LOG_INF("Sent config data back");
			}
		}

		bool new_io = false;
		bool io_tx_pending = reg_tx_pending_for(PORT_ID_IO);

		sl = sizeof(port_io);
		tx_res = flush_tx(sock, PORT_ID_IO, &port_io, sizeof(port_io));
		has_job += tx_res > 0 ? 1 : 0;

		if (!io_tx_pending && port_rx_needs_service(ready_mask, PORT_ID_IO) &&
		    fault_delay_ready(PORT_ID_IO, false)) {
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

		if (!io_tx_pending && (ready_mask != 0 || new_io)) {
			uint8_t io_port = PORT_ID_IO;
			uint8_t *io_tx = REG_BUF_REG(io);
			uint16_t io_len = REG_BUF_LEN(io);

			io_tx[5]++;
			tx_res = send_tx(sock, io_port, io_tx, io_len, &port_io, sizeof(port_io));
			has_job += tx_res > 0 ? 1 : 0;
		}

		has_job += service_ethernet_ports(sock, ready_mask);
		has_job += handle_extra_errors(sock);

		if (has_job == 0) {
			k_usleep(10);
		}

		/* NOTE: terminate condition */
		if (k_sem_take(&terminate_sem, K_NO_WAIT) == 0) {
			zsock_close(sock);
			break;
		}
	}
	reset_slave_pending_state();
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
