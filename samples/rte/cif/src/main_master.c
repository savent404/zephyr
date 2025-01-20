/**
 * @license This project is CLOSED SOURCE, All Rights Reserved
 * Copyright (c) 2025 SYSTech Co.
 */
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/socketcif.h>
#include <zephyr/logging/log.h>

#define SYNC_CYCLE_TIME         2000   /* 2ms */
#define ASYNC_INTERVAL_TIME     10000  /* 10ms */
#define ASYNC_TIMEOUT_TIME      100000 /* 100ms */
#define ASYNC_DEFAULT_BANDWIDTH 0      /* no limitation */

#if !CONFIG_CIF_WORKAROUND
#define PORT_ID_CFG 0x10
#define PORT_ID_IO  0x40
#else
#define PORT_ID_CFG 1
#define PORT_ID_IO  2
#endif

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

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
		LOG_INF("Failed to configure CIF socket port, errno %d\n", errno);
		return false;
	}

	/* Wait for LDP query discovery response automatically */
	k_sleep(K_USEC(SYNC_CYCLE_TIME));

	static uint8_t buf[CIF_MTU];
	ssize_t len;

	len = recvfrom(cif_sock, buf, sizeof(buf), 0, NULL, 0);

	if (len < 0) {
		LOG_INF("Failed to receive data, errno %d\n", errno);
		exist = false;
	} else {
		LOG_INF("Received %zd bytes\n", len);
		exist = true;
	}

	/* close connection */
	port_cfg.flags &= ~CIF_PORT_FLG_ENABLE;
	ret = setsockopt(cif_sock, SOL_CIF_RAW, CIF_OPT_PORT, &port_cfg, sizeof(port_cfg));
	if (ret < 0) {
		LOG_INF("Failed to configure CIF socket port, errno %d\n", errno);
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
	int sock = socket(AF_CIF, SOCK_RAW, CIF_RAW_MASTER);
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
	 * Step 3: set the CIF socket options
	 */
	const struct cif_raw_master_config config = {
		.poll_time = 50,        /* 50us */
		.cycle_time = 1000 * 5, /* 5ms */
	};
	ret = setsockopt(sock, SOL_CIF_RAW, CIF_OPT_MASTER_CONFIG, &config, sizeof(config));
	if (ret < 0) {
		LOG_INF("Failed to set CIF socket options, errno %d\n", errno);
		close(sock);
		return -1;
	}

	/**
	 * Step 4: device discovery
	 */
	bool slot1_exist = dev_discovery(sock, 1);
	bool slot2_exist = dev_discovery(sock, 2);
	bool exist[] = {slot1_exist, slot2_exist};

	LOG_INF("Slot 1 %s\n", slot1_exist ? "exist" : "not exist");
	LOG_INF("Slot 2 %s\n", slot2_exist ? "exist" : "not exist");

	if (!slot1_exist && !slot2_exist) {
		LOG_INF("No device found\n");
		LOG_INF("Assume the slot2 is alive, and do the general initialization\n");
		exist[1] = true;
	}

	/**
	 * Step 5: Do the general initialization
	 */
	for (uint8_t slot_idx = 0; slot_idx < 2; slot_idx++) {
		uint8_t slot = slot_idx + 1;

		if (!exist[slot_idx]) {
			continue;
		}

		/* establish sync connection */
		if (dev_general_init(sock, slot, PORT_ID_IO, 0)) {
			struct sockaddr_cif remote = {
				.cif_family = AF_CIF,
				.slot = slot,
				.port = PORT_ID_IO,
			};
			ret = sendto(sock, "Hello", 5, 0, (struct sockaddr *)&remote,
				     sizeof(remote));
			if (ret < 0) {
				LOG_INF("Failed to send data, errno %d\n", errno);
			}
		}

		uint32_t max_try = 10;

		/* Do configuration */
		if (dev_general_init(sock, slot, PORT_ID_CFG, CIF_PORT_FLG_STRONG_ORDER)) {
			static const char config_data[] = "some configure";
			char response[64];
			struct sockaddr_cif remote = {
				.cif_family = AF_CIF,
				.slot = slot,
				.port = PORT_ID_CFG,
			};
			socklen_t addrlen = sizeof(remote);

			ret = sendto(sock, config_data, sizeof(config_data), 0,
				     (struct sockaddr *)&remote, sizeof(remote));
			if (ret < 0) {
				LOG_INF("Failed to send data, errno %d\n", errno);
			}

			/* Wait till the configuration is done, wait for the response */
			while (--max_try) {
				int len = recvfrom(sock, response, sizeof(response), 0,
						   (struct sockaddr *)&remote,
						   (socklen_t *)(&addrlen));

				if (ret < 0 && errno == EAGAIN) {
					LOG_DBG("Wait for configuration done\n");
				} else if (ret < 0) {
					LOG_ERR("Failed to receive data, errno %d\n", errno);
				} else {
					LOG_INF("Received %d bytes\n", len);
					if (!memcpy(response, config_data, sizeof(config_data)) ==
					    0) {
						LOG_ERR("Configuration failed\n");
					}
					break;
				}
				k_sleep(K_USEC(ASYNC_INTERVAL_TIME));
			}

			if (!max_try) {
				LOG_INF("Configuration timeout, It is okay if using the "
					"simulation\n");
			}
		}
	}

	while (1) {
		static uint8_t in_buf[2][64], out_buf[2][64];

		/* Sync Input */
		for (uint8_t slot = 0; slot < 2; slot++) {
			if (!exist[slot]) {
				continue;
			}

			struct sockaddr_cif remote = {
				.cif_family = AF_CIF,
				.slot = slot,
				.port = PORT_ID_IO,
			};

			ret = sendto(sock, out_buf[slot], sizeof(out_buf[slot]), 0,
				     (struct sockaddr *)&remote, sizeof(remote));
			if (ret < 0) {
				LOG_INF("Failed to send data, errno %d\n", errno);
				/* Application need to handle the error */
			}
		}

		/* detect none-critical error */
		uint32_t errors;
		struct cif_error_filter filter = {
			.flags = 0, /* no filter, applies to all connections */
		};
		socklen_t opt_len = sizeof(filter);

		ret = getsockopt(sock, SOL_CIF_RAW, CIF_OPT_ERROR, &filter, &opt_len);
		if (ret < 0) {
			LOG_INF("Failed to get error, errno %d\n", errno);
			/* Application need to handle the error */
		}
		errors = filter.error_mask;

		if (errors) {
			LOG_INF("Error detected: 0x%08x\n", errors);
			/* Application need to handle error
			 * For example, if some connection is got I_ERROR, we need to find out which
			 * connection it is
			 */

			/* Find out the connection
			 * for (auto conn : connections) {
			 *	filter.slot = conn.slot;
			 *	filter.port = conn.port;
			 *	filter.flags =
			 *		CIF_FILTER_SLOT | CIF_FILTER_PORT; // filter by slot and
			 *	port ret = setsockopt(sock, SOL_CIF_RAW, CIF_OPT_ERROR, &filter,
			 *			      sizeof(filter));
			 *	if (ret < 0) {
			 *		LOG_INF("Failed to set error filter, errno %d\n", errno);
			 *	}
			 *	if (errors & I_ERROR) {
			 *		// handle the error
			 *	}
			 * }
			 */

			/* Clear errors if handled */
			uint32_t handled_errors = errors;

			filter.flags = 0; /* no filter, applies to all connections */
			filter.error_mask = handled_errors;
			LOG_INF("Error handled: 0x%08x\n", handled_errors);
			ret = setsockopt(sock, SOL_CIF_RAW, CIF_OPT_ERROR, &filter, sizeof(filter));
		}

		/* Application here
		 * application(in_buf, out_buf, ...other handles);
		 */

		/* Sync Output */
		for (uint8_t slot = 0; slot < 2; slot++) {
			if (!exist[slot]) {
				continue;
			}

			struct sockaddr_cif remote = {
				.cif_family = AF_CIF,
				.slot = slot,
				.port = PORT_ID_IO,
			};
			int len = sendto(sock, in_buf[slot], sizeof(in_buf[slot]), 0,
					 (struct sockaddr *)&remote, sizeof(remote));

			if (len < 0) {
				LOG_INF("Failed to send data, errno %d\n", errno);
				/* Application need to handle the error */
			}
		}

		/* loop interval: SYNC_CYCLE_TIME(2ms) */
		k_sleep(K_USEC(SYNC_CYCLE_TIME));
	}

	close(sock);

	while (1)
		;
	return 0;
}
