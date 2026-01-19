/*
 * Copyright (c) 2026 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "disk_device.h"
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <string.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_if.h>

LOG_MODULE_REGISTER(disk_tools_net, CONFIG_DISK_TOOLS_LOG_LEVEL);

/* Network transfer context */
static struct {
	const struct disk_device *device;
	size_t offset;
	size_t length;
	bool active;
	int server_fd;
	struct k_thread server_thread;
} net_ctx;

/* Static buffer for data transfer to avoid stack overflow */
static uint8_t transfer_buffer[CONFIG_DISK_TOOLS_BUFFER_SIZE] __aligned(4);

/* Thread stack */
K_THREAD_STACK_DEFINE(net_stack, CONFIG_DISK_TOOLS_NET_STACK_SIZE);

/* Semaphore to ensure only one transfer thread can be active */
static K_SEM_DEFINE(thread_done_sem, 1, 1);

/* Protocol commands - compatible with coredump_client.py */
#define CMD_GET_SIZE "GET_SIZE"
#define CMD_GET_DATA "GET_DATA"
#define CMD_QUIT     "QUIT"

/* Response codes */
#define RESP_OK      "OK"
#define RESP_ERROR   "ERROR"
#define RESP_NO_DATA "NO_DUMP"

/**
 * @brief Send response to client
 */
static int send_response(int client_fd, const char *response)
{
	int ret;
	size_t len = strlen(response);

	ret = send(client_fd, response, len, 0);
	if (ret < 0) {
		/* Only log error if it's not a "connection closed" error */
		if (errno != ENOTCONN && errno != ECONNRESET && errno != EPIPE) {
			LOG_ERR("Failed to send response: %d", errno);
		}
		return -errno;
	}

	/* Send newline */
	ret = send(client_fd, "\n", 1, 0);
	if (ret < 0) {
		/* Only log error if it's not a "connection closed" error */
		if (errno != ENOTCONN && errno != ECONNRESET && errno != EPIPE) {
			LOG_ERR("Failed to send newline: %d", errno);
		}
		return -errno;
	}

	return 0;
}

/**
 * @brief Handle GET_SIZE command
 */
static int handle_get_size(int client_fd)
{
	char buf[32];

	if (!net_ctx.active || net_ctx.length == 0) {
		return send_response(client_fd, RESP_NO_DATA);
	}

	snprintf(buf, sizeof(buf), "SIZE:%zu", net_ctx.length);
	return send_response(client_fd, buf);
}

/**
 * @brief Handle GET_DATA command
 */
static int handle_get_data(int client_fd)
{
	int ret;
	uint8_t *chunk = transfer_buffer; /* Use static buffer */
	size_t offset;
	size_t remaining;
	size_t sent_bytes = 0;
	uint32_t size_le;
	int64_t last_report_time = 0;
	int64_t current_time;
	const int64_t time_interval_ms = 1000; /* Report at least every 1 second */
	const size_t chunk_size = CONFIG_DISK_TOOLS_BUFFER_SIZE;

	if (!net_ctx.active || net_ctx.length == 0) {
		return send_response(client_fd, RESP_NO_DATA);
	}

	/* Send OK to start transfer */
	ret = send_response(client_fd, RESP_OK);
	if (ret < 0) {
		return ret;
	}

	/* Send size in little-endian format (compatible with Python struct.unpack('<I')) */
	size_le = (uint32_t)net_ctx.length;
	ret = send(client_fd, &size_le, sizeof(size_le), 0);
	if (ret < 0) {
		LOG_ERR("Failed to send size: %d", errno);
		return -errno;
	}

	LOG_INF("Starting data transfer: %zu bytes from offset 0x%zx", net_ctx.length,
		net_ctx.offset);

	/* Send data in chunks */
	offset = net_ctx.offset;
	remaining = net_ctx.length;
	last_report_time = k_uptime_get();

	while (remaining > 0) {
		size_t read_size = MIN(remaining, chunk_size);
		size_t to_send;
		uint8_t *send_ptr;

		/* Read from device */
		ret = net_ctx.device->ops->read(offset, chunk, read_size);
		if (ret < 0) {
			LOG_ERR("Read error at offset 0x%zx: %d", offset, ret);
			return ret;
		}

		if (ret == 0) {
			LOG_ERR("No data read at offset 0x%zx", offset);
			break;
		}

		/* Send all data in the chunk (handle partial sends) */
		to_send = ret;
		send_ptr = chunk;

		while (to_send > 0) {
			/* Use blocking send for better performance */
			ret = send(client_fd, send_ptr, to_send, 0);
			if (ret < 0) {
				LOG_ERR("Failed to send chunk: %d", errno);
				return -errno;
			}

			if (ret == 0) {
				LOG_ERR("Connection closed during send");
				return -ECONNRESET;
			}

			send_ptr += ret;
			to_send -= ret;
			sent_bytes += ret;
			offset += ret;
			remaining -= ret;
		}

		/* Log progress based on time interval (every second) or completion */
		current_time = k_uptime_get();
		if (remaining == 0) {
			/* Always print completion */
			LOG_INF("Sent %zu/%zu bytes (%.1f%%)", sent_bytes, net_ctx.length,
				(double)(((float)sent_bytes / net_ctx.length) * 100));
		} else if ((current_time - last_report_time) >= time_interval_ms) {
			/* Print progress every second */
			LOG_INF("Sent %zu/%zu bytes (%.1f%%)", sent_bytes, net_ctx.length,
				(double)(((float)sent_bytes / net_ctx.length) * 100));
			last_report_time = current_time;
		}
	}

	LOG_INF("Transfer complete: %zu bytes sent", sent_bytes);
	return 0;
}

/**
 * @brief Receive command from client
 */
static int recv_command(int client_fd, char *buf, size_t buf_size)
{
	size_t pos = 0;
	struct timeval timeout;

	/* Set receive timeout from Kconfig */
	timeout.tv_sec = CONFIG_DISK_TOOLS_NET_TIMEOUT_MS / 1000;
	timeout.tv_usec = (CONFIG_DISK_TOOLS_NET_TIMEOUT_MS % 1000) * 1000;

	if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
		LOG_WRN("Failed to set socket timeout: %d", errno);
	}

	while (pos < buf_size - 1) {
		int ret = recv(client_fd, &buf[pos], 1, 0);

		if (ret < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				LOG_ERR("Command receive timeout");
				return -ETIMEDOUT;
			}
			return -errno;
		}

		if (ret == 0) {
			/* Connection closed */
			return -ECONNRESET;
		}

		if (buf[pos] == '\n') {
			buf[pos] = '\0';
			return 0;
		}

		pos++;
	}

	/* Command too long */
	return -E2BIG;
}

/**
 * @brief Handle client connection
 */
static void handle_client(int client_fd)
{
	char cmd_buf[32];
	int ret;

	LOG_INF("Client connected");

	while (1) {
		/* Receive command */
		ret = recv_command(client_fd, cmd_buf, sizeof(cmd_buf));
		if (ret != 0) {
			if (ret == -ECONNRESET) {
				LOG_INF("Client disconnected");
			} else {
				LOG_ERR("Failed to receive command: %d", ret);
			}
			break;
		}

		LOG_DBG("Received command: %s", cmd_buf);

		if (strcmp(cmd_buf, CMD_QUIT) == 0) {
			send_response(client_fd, RESP_OK);
			LOG_INF("Client requested quit");
			break;
		}

		/* Handle command */
		if (strcmp(cmd_buf, CMD_GET_SIZE) == 0) {
			ret = handle_get_size(client_fd);
		} else if (strcmp(cmd_buf, CMD_GET_DATA) == 0) {
			ret = handle_get_data(client_fd);
		} else {
			LOG_WRN("Unknown command: %s", cmd_buf);
			ret = send_response(client_fd, RESP_ERROR);
		}

		if (ret != 0) {
			LOG_ERR("Command handler failed: %d", ret);
			break;
		}
	}

	close(client_fd);
	LOG_INF("Client connection closed");
}

/**
 * @brief Network server thread entry
 */
static void server_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct sockaddr_in bind_addr;
	int ret;

	/* Create socket */
	net_ctx.server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (net_ctx.server_fd < 0) {
		LOG_ERR("Failed to create socket: %d", errno);
		return;
	}

	/* Enable TCP_NODELAY to disable Nagle's algorithm for better performance */
	int nodelay = 1;

	ret = setsockopt(net_ctx.server_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
	if (ret < 0) {
		LOG_DBG("TCP_NODELAY not supported: %d", errno);
	} else {
		LOG_DBG("TCP_NODELAY enabled");
	}

	/* Enable address reuse */
	int optval = 1;

	ret = setsockopt(net_ctx.server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	if (ret < 0) {
		LOG_DBG("SO_REUSEADDR not supported: %d", errno);
	}

	/* Bind */
	memset(&bind_addr, 0, sizeof(bind_addr));
	bind_addr.sin_family = AF_INET;
	bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	bind_addr.sin_port = htons(CONFIG_DISK_TOOLS_NET_PORT);

	ret = bind(net_ctx.server_fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
	if (ret < 0) {
		LOG_ERR("Failed to bind: %d", errno);
		close(net_ctx.server_fd);
		return;
	}

	/* Listen */
	ret = listen(net_ctx.server_fd, 1);
	if (ret < 0) {
		LOG_ERR("Failed to listen: %d", errno);
		close(net_ctx.server_fd);
		return;
	}

	LOG_INF("Server ready, waiting for connection...");

	printk("\n");
	printk("=================================================\n");
	printk("Disk Tools Network Server Ready\n");
	printk("=================================================\n");
	printk("Port:   %d\n", CONFIG_DISK_TOOLS_NET_PORT);
	printk("Device: %s\n", net_ctx.device->ops->get_name());
	printk("Offset: 0x%zx (%zu bytes)\n", net_ctx.offset, net_ctx.offset);
	printk("Length: %zu bytes (%.2f KB)\n", net_ctx.length, net_ctx.length / 1024.0);
	printk("\n");
	printk("Connect with:\n");
	printk("  python3 coredump_client.py <device_ip> -p %d -o output.bin\n",
	       CONFIG_DISK_TOOLS_NET_PORT);
	printk("=================================================\n");
	printk("\n");

	/* Accept one client */
	struct sockaddr_in client_addr;
	socklen_t client_addr_len = sizeof(client_addr);

	int client_fd =
		accept(net_ctx.server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
	if (client_fd < 0) {
		LOG_ERR("Failed to accept: %d", errno);
		close(net_ctx.server_fd);
		return;
	}

	/* Handle client */
	handle_client(client_fd);

	/* Cleanup */
	close(net_ctx.server_fd);
	net_ctx.active = false;

	LOG_INF("Network transfer server stopped");
	printk("\nServer stopped. Transfer complete.\n");

	/* Signal thread is completely done */
	k_sem_give(&thread_done_sem);
}

/**
 * @brief Start network transfer
 */
int disk_tools_network_transfer(const struct disk_device *dev, size_t offset, size_t length)
{
	k_tid_t tid;

	if (dev == NULL || dev->ops == NULL) {
		return -EINVAL;
	}

	/* Wait for previous thread to complete */
	if (k_sem_take(&thread_done_sem, K_NO_WAIT) != 0) {
		LOG_ERR("Network transfer already active");
		return -EBUSY;
	}

	/* Initialize context */
	net_ctx.device = dev;
	net_ctx.offset = offset;
	net_ctx.length = length;
	net_ctx.active = true;

	/* Start server thread */
	tid = k_thread_create(&net_ctx.server_thread, net_stack, K_THREAD_STACK_SIZEOF(net_stack),
			      server_thread_entry, NULL, NULL, NULL,
			      CONFIG_DISK_TOOLS_NET_THREAD_PRIORITY, 0, K_NO_WAIT);

	if (tid == NULL) {
		LOG_ERR("Failed to create server thread");
		net_ctx.active = false;
		k_sem_give(&thread_done_sem);
		return -ENOMEM;
	}

	k_thread_name_set(tid, "disk_net_srv");

	return 0;
}
