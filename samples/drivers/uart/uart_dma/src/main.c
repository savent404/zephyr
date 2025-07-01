/*
 * Copyright (c) 2025 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys_clock.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/cache.h>
#include <zephyr/timing/timing.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(uart_dma_sample, LOG_LEVEL_INF);

#define UART_DEVICE_NODE DT_NODELABEL(uart1)

/* Increase buffer sizes to demonstrate DMA advantage */
#define RECEIVE_TIMEOUT     200000 /* Timeout in microseconds */
#define RECEIVE_BUFFER_SIZE 64

static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

/* TX and RX buffers */
static __aligned(32) char rx_buf[RECEIVE_BUFFER_SIZE];
static __aligned(32) char tx_buf[RECEIVE_BUFFER_SIZE];
static volatile bool rx_buf_request;
static volatile bool transfer_complete;
static volatile bool receive_ready;
static volatile bool receive_done;
static struct k_sem tx_done_sem;
static struct k_sem rx_rdy_sem;
static volatile bool dma_error_detected;
static volatile size_t rx_data_len;
static volatile bool rx_timeout_occurred;

/* UART callback function */
static void uart_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
	switch (evt->type) {
	case UART_TX_DONE:
		LOG_DBG("TX completed: %d bytes", evt->data.tx.len);
		transfer_complete = true;
		k_sem_give(&tx_done_sem);
		break;
	case UART_TX_ABORTED:
		LOG_ERR("TX aborted");
		dma_error_detected = true;
		k_sem_give(&tx_done_sem);
		break;
	case UART_RX_RDY:
		LOG_DBG("RX ready: %d bytes at offset %d", evt->data.rx.len, evt->data.rx.offset);
		receive_ready = true;
		rx_data_len = evt->data.rx.len;
		rx_timeout_occurred = false;
		k_sem_give(&rx_rdy_sem);
		break;
	case UART_RX_BUF_REQUEST:
		LOG_DBG("RX buffer request");
		rx_buf_request = true;
		break;
	case UART_RX_BUF_RELEASED:
		LOG_DBG("RX buffer released");
		break;
	case UART_RX_DISABLED:
		LOG_DBG("RX disabled");
		receive_done = true;
		if (!receive_ready) {
			rx_timeout_occurred = true;
		}
		k_sem_give(&rx_rdy_sem);
		break;
	case UART_RX_STOPPED:
		LOG_ERR("RX stopped, reason: %d", evt->data.rx_stop.reason);
		dma_error_detected = true;
		k_sem_give(&rx_rdy_sem);
		break;
	}
}

static size_t test_uart_rx(void)
{
	int ret;

	memset(rx_buf, 0, sizeof(rx_buf));

	rx_buf_request = false;
	receive_ready = false;
	receive_done = false;
	dma_error_detected = false;
	rx_timeout_occurred = false;
	rx_data_len = 0;

	ret = uart_rx_enable(uart_dev, rx_buf, sizeof(rx_buf), RECEIVE_TIMEOUT);
	if (ret < 0) {
		LOG_ERR("Failed to start UART reception: %d", ret);
		return 0;
	}

	LOG_DBG("DMA RX started successfully");

	ret = k_sem_take(&rx_rdy_sem, K_USEC(RECEIVE_TIMEOUT + 10000));

	uart_rx_disable(uart_dev);

	if (ret == 0) {
		if (receive_ready && rx_data_len > 0 && !rx_timeout_occurred) {
			LOG_INF("RX successful: %d bytes received", rx_data_len);
			LOG_HEXDUMP_INF(rx_buf, rx_data_len, "RX Data");
			return rx_data_len;
		}
		if (rx_timeout_occurred) {
			LOG_DBG("RX timeout - no new data received");
			return 0;
		}
		LOG_DBG("RX completed but no valid data");
		return 0;
	}

	LOG_DBG("RX wait timeout - no data received");
	return 0;
}

static void dma_tx_echo(const char *buf, size_t len)
{
	int ret;

	if (len == 0) {
		LOG_DBG("No data to echo");
		return;
	}

	LOG_DBG("Echoing %d bytes via DMA", len);

	transfer_complete = false;
	dma_error_detected = false;
	k_sem_reset(&tx_done_sem);

	ret = uart_tx(uart_dev, buf, len, SYS_FOREVER_US);
	if (ret) {
		LOG_ERR("uart_tx failed: %d", ret);
		return;
	}

	if (k_sem_take(&tx_done_sem, K_MSEC(10000)) == 0 && !dma_error_detected) {
		LOG_DBG("Echo TX completed successfully");
	} else {
		LOG_ERR("Echo TX failed (complete=%d, error=%d)", transfer_complete,
			dma_error_detected);
	}
}

int main(void)
{
	int ret;

	LOG_INF("UART DMA Echo Demo Starting");

	if (!device_is_ready(uart_dev)) {
		LOG_ERR("UART device not ready");
		return -1;
	}

	k_sem_init(&tx_done_sem, 0, 1);
	k_sem_init(&rx_rdy_sem, 0, 1);

	ret = uart_callback_set(uart_dev, uart_callback, NULL);
	if (ret < 0) {
		LOG_ERR("Failed to set UART callback: %d", ret);
		return -1;
	}

	LOG_INF("UART DMA initialized successfully");

	const char *prompt = "open rtt viewer to see the log\r\n"
			     "enter text in uart terminal to test dma echo...\r\n";
	uart_tx(uart_dev, prompt, strlen(prompt), SYS_FOREVER_US);
	k_sem_take(&tx_done_sem, K_MSEC(5000));

	for (int i = 0; i < 0xffff; i++) {
		LOG_DBG("=== Echo Demo Round %d ===", i + 1);
		size_t len = test_uart_rx();

		if (len > 0) {
			memset(tx_buf, 0, sizeof(tx_buf));
			memcpy(tx_buf, rx_buf, len);
			dma_tx_echo(tx_buf, len);
		} else {
			LOG_DBG("No data received in round %d", i + 1);
		}
		k_sleep(K_MSEC(10));
	}

	return 0;
}
