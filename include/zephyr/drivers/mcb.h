/**
 * @file mcb.h
 * @author Liao YuanKai(savent_gate@outlook.com)
 * @date 2025-01-07
 * @copyright Copyright (c) 2025
 * @license This project is CLOSED SOURCE, All Rights Reserved
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_MCB_H_
#define ZEPHYR_INCLUDE_DRIVERS_MCB_H_

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#ifdef __cplusplus
extern "C" {
#endif

#define _MCB_ERR_OK        0
#define _MCB_ERR_T_ERR     (BIT(0)) /* Timeout */
#define _MCB_ERR_R_ERR     (BIT(1)) /* Redundancy error(lost one of the frames) */
#define _MCB_ERR_I_ERR     (BIT(2)) /* Invalid frame */
#define _MCB_ERR_MUL_FRAME (BIT(3)) /* Multiple frames received */
#define _MCB_ERR_PREEMPT   (BIT(4)) /* Preempted by R=1 frame */
#define _MCB_ERR_P_ERR     (BIT(5)) /* Port rejected */

#define _MCB_ROLE_MASTER 0
#define _MCB_ROLE_SLAVE  1

/** @cond INTERNAL_HIDDEN */

/**
 * @brief Reset MCB to initial state
 *
 * @param dev MCB device instance
 * @param role see @c _MCB_ROLE_MASTER or @c _MCB_ROLE_SLAVE
 *
 * @return See the return values for mcb_reset()
 * @see mcb_reset()
 */
typedef void (*mcb_reset_t)(const struct device *dev, uint8_t role);

/**
 * @brief Set poll time for MCB
 *
 * @param dev MCB device instance
 * @param timeout timeout in microseconds (us)
 *
 * @return See the return values for mcb_poll_time()
 * @see mcb_poll_time()
 */
typedef void (*mcb_poll_time_t)(const struct device *dev, uint32_t timeout);

/**
 * @brief Configure MCB port
 *
 * @param dev MCB device instance
 * @param port Port number
 * @param enable Enable or disable the port
 * @param write Accept write or read-only. Only valid when role is @c _MCB_ROLE_SLAVE.
 * @param max_rx_len Maximum length of the received frame. Only valid when role is @c
 * _MCB_ROLE_SLAVE.
 *
 * @return See the return values for mcb_config_port()
 * @see mcb_config_port()
 */
typedef void (*mcb_config_port_t)(const struct device *dev, uint8_t port, bool enable, bool write,
				  uint16_t max_rx_len);

/**
 * @brief Get status register of MCB
 *
 * @param dev MCB device instance
 * @param status status register. See @c _MCB_ERR_*
 *
 * @return See the return values for mcb_get_status()
 * @see mcb_get_status()
 */
typedef void (*mcb_get_status_t)(const struct device *dev, uint32_t *status);

/**
 * @brief Clear status bits
 *
 * @param dev MCB device instance
 * @param bits bits need to be cleared, see @c _MCB_ERR_*
 *
 * @return See the return values for mcb_clr_status()
 * @see mcb_clr_status()
 */
typedef void (*mcb_clr_status_t)(const struct device *dev, uint32_t bits);

/**
 * @brief Check if the port received a frame
 *
 * Firstly, this function will check global status register to see if there is any frame.
 * MCB's Register @c _MCB_PORT_RDY_MASK_* is also used to check if the port received a frame.
 *
 * @param dev MCB device instance
 * @param port port number
 *
 * @return See the return values for mcb_rx_ready()
 * @see mcb_rx_ready()
 */
typedef int (*mcb_rx_is_ready_t)(const struct device *dev, uint8_t port);

/**
 * @brief Clear the port's received frame status
 *
 * This function will clear the port's received frame status.
 *
 * @param dev MCB device instance
 * @param port port number
 *
 * @return See the return values for mcb_rx_clr()
 * @see mcb_rx_clr()
 */
typedef void (*mcb_rx_clr_t)(const struct device *dev, uint8_t port);

/**
 * @brief Trigger transmission to specific sid and port.
 *
 * This function will trigger the transmission to specific sid and port.
 * It will send the frame from tx buffer on the port.
 *
 * @param dev MCB device instance
 * @param sid destination sid
 * @param port port number
 * @param preempt preempt flag
 *
 * @return see the return values for mcb_tx()
 * @see mcb_tx()
 */
typedef void (*mcb_tx_t)(const struct device *dev, uint8_t sid, uint8_t port, bool preempt);

/**
 * @brief Get the receive buffer of the port
 *
 * This function will return the receive buffer of the port.
 *
 * @param dev MCB device instance
 * @param port port number
 *
 * @return receive buffer
 */
typedef void *(*mcb_get_rx_buf_t)(const struct device *dev, uint8_t port);

/**
 * @brief Get the transmit buffer of the port
 *
 * This function will return the transmit buffer of the port.
 *
 * @param dev MCB device instance
 * @param port port number
 *
 * @return transmit buffer
 */
typedef void *(*mcb_get_tx_buf_t)(const struct device *dev, uint8_t port);

/**
 * @brief Get the length of the received frame
 *
 * This function will return the length of the received frame.
 *
 * @param dev MCB device instance
 * @param port port number
 *
 * @return length of the received frame
 */
typedef uint16_t (*mcb_get_rx_len_t)(const struct device *dev, uint8_t port);

/**
 * @brief Get the length of the transmit frame
 *
 * This function will return the length of the transmit frame.
 *
 * @param dev MCB device instance
 * @param port port number
 *
 * @return length of the transmit frame
 */
typedef uint16_t (*mcb_get_tx_len_t)(const struct device *dev, uint8_t port);

/**
 * @brief Set the length of the transmit frame
 *
 * This function will set the length of the transmit frame.
 *
 * @param dev MCB device instance
 * @param port port number
 * @param len length of the transmit frame
 */
typedef void (*mcb_set_tx_len_t)(const struct device *dev, uint8_t port, uint16_t len);

/** @endcond */

__subsystem struct mcb_driver_api {
	mcb_reset_t reset;
	mcb_poll_time_t poll_time;
	mcb_config_port_t config_port;
	mcb_get_status_t get_status;
	mcb_clr_status_t clr_status;
	mcb_rx_is_ready_t rx_is_ready;
	mcb_rx_clr_t rx_clr;
	mcb_tx_t tx;
	mcb_get_rx_buf_t get_rx_buf;
	mcb_get_tx_buf_t get_tx_buf;
	mcb_get_rx_len_t get_rx_len;
	mcb_get_tx_len_t get_tx_len;
	mcb_set_tx_len_t set_tx_len;
};
/** @endcond */

/**
 * @brief Reset MCB to initial state
 *
 * This function will reset the MCB to initial state and clear all the status.
 * Also, it will chose the role of the MCB (master or slave).
 *
 * @param dev MCB device instance
 * @param role see @c _MCB_ROLE_MASTER or @c _MCB_ROLE_SLAVE
 */
__syscall void mcb_reset(const struct device *dev, uint8_t role);

static inline void z_impl_mcb_reset(const struct device *dev, uint8_t role)
{
	const struct mcb_driver_api *api = (const struct mcb_driver_api *)dev->api;

	if (api->reset == NULL) {
		return;
	}

	api->reset(dev, role);
}

/**
 * @brief Set poll time for MCB
 *
 * This function will set the poll time for MCB, which is the timeout for the
 * MCB to wait for the frame.
 *
 * @param dev MCB device instance
 * @param timeout timeout in microseconds (us)
 * @retval 0 on success
 */
__syscall void mcb_poll_time(const struct device *dev, uint32_t timeout);

static inline void z_impl_mcb_poll_time(const struct device *dev, uint32_t timeout)
{
	const struct mcb_driver_api *api = (const struct mcb_driver_api *)dev->api;

	if (api->poll_time == NULL) {
		return;
	}

	api->poll_time(dev, timeout);
}

/**
 * @brief Configure MCB port
 *
 * This function will configure the MCB port, which includes enabling or
 * disabling the port, allowing write or read-only, and setting the maximum
 * length of the received frame.
 *
 * @param dev MCB device instance
 * @param port Port number
 * @param enable Enable or disable the port
 * @param write Accept write or read-only. Only valid when role is @c _MCB_ROLE_SLAVE.
 * @param max_rx_len Maximum length of the received frame. Only valid when role is @c
 * _MCB_ROLE_SLAVE.
 */
__syscall void mcb_config_port(const struct device *dev, uint8_t port, bool enable, bool write,
			       uint16_t max_rx_len);

static inline void z_impl_mcb_config_port(const struct device *dev, uint8_t port, bool enable,
					  bool write, uint16_t max_rx_len)
{
	const struct mcb_driver_api *api = (const struct mcb_driver_api *)dev->api;

	if (api->config_port == NULL) {
		return;
	}

	api->config_port(dev, port, enable, write, max_rx_len);
}

/**
 * @brief Get status register of MCB
 *
 * This function will get the status register of MCB, which includes the error
 * bits.
 *
 * @param dev MCB device instance
 * @param status status register. See @c _MCB_ERR_*
 */
__syscall void mcb_get_status(const struct device *dev, uint32_t *status);

static inline void z_impl_mcb_get_status(const struct device *dev, uint32_t *status)
{
	const struct mcb_driver_api *api = (const struct mcb_driver_api *)dev->api;

	if (api->get_status == NULL) {
		return;
	}

	api->get_status(dev, status);
}

/**
 * @brief Clear status bits
 *
 * This function will clear the status bits of MCB.
 *
 * @param dev MCB device instance
 * @param bits bits need to be cleared, see @c _MCB_ERR_*
 */
__syscall void mcb_clr_status(const struct device *dev, uint32_t bits);

static inline void z_impl_mcb_clr_status(const struct device *dev, uint32_t bits)
{
	const struct mcb_driver_api *api = (const struct mcb_driver_api *)dev->api;

	if (api->clr_status == NULL) {
		return;
	}

	api->clr_status(dev, bits);
}

/**
 * @brief Check if the port received a frame
 *
 * This function will check if the port received a frame.
 *
 * @param dev MCB device instance
 * @param port port number
 * @retval 0 rx is not ready
 * @retval 1 rx is ready
 */
__syscall int mcb_rx_is_ready(const struct device *dev, uint8_t port);

static inline int z_impl_mcb_rx_is_ready(const struct device *dev, uint8_t port)
{
	const struct mcb_driver_api *api = (const struct mcb_driver_api *)dev->api;

	if (api->rx_is_ready == NULL) {
		return -ENOSYS;
	}

	return api->rx_is_ready(dev, port);
}

/**
 * @brief Clear the port's received frame status
 *
 * This function will clear the port's received frame status.
 *
 * @param dev MCB device instance
 * @param port port number
 */
__syscall void mcb_rx_clr(const struct device *dev, uint8_t port);

static inline void z_impl_mcb_rx_clr(const struct device *dev, uint8_t port)
{
	const struct mcb_driver_api *api = (const struct mcb_driver_api *)dev->api;

	if (api->rx_clr == NULL) {
		return;
	}

	api->rx_clr(dev, port);
}

/**
 * @brief Trigger transmission to specific sid and port.
 *
 * This function will trigger the transmission to specific sid and port.
 * It will send the frame from tx buffer on the port.
 *
 * @param dev MCB device instance
 * @param sid destination sid
 * @param port port number
 * @param preempt preempt flag
 */
__syscall void mcb_tx(const struct device *dev, uint8_t sid, uint8_t port, bool preempt);

static inline void z_impl_mcb_tx(const struct device *dev, uint8_t sid, uint8_t port, bool preempt)
{
	const struct mcb_driver_api *api = (const struct mcb_driver_api *)dev->api;

	if (api->tx == NULL) {
		return;
	}

	api->tx(dev, sid, port, preempt);
}

/**
 * @brief Get the receive buffer of the port
 *
 * This function will return the receive buffer of the port.
 *
 * @param dev MCB device instance
 * @param port port number
 * @return receive buffer
 */
__syscall void *mcb_get_rx_buf(const struct device *dev, uint8_t port);

static inline void *z_impl_mcb_get_rx_buf(const struct device *dev, uint8_t port)
{
	const struct mcb_driver_api *api = (const struct mcb_driver_api *)dev->api;

	if (api->get_rx_buf == NULL) {
		return NULL;
	}

	return api->get_rx_buf(dev, port);
}

/**
 * @brief Get the transmit buffer of the port
 *
 * This function will return the transmit buffer of the port.
 *
 * @param dev MCB device instance
 * @param port port number
 * @return transmit buffer
 */
__syscall void *mcb_get_tx_buf(const struct device *dev, uint8_t port);

static inline void *z_impl_mcb_get_tx_buf(const struct device *dev, uint8_t port)
{
	const struct mcb_driver_api *api = (const struct mcb_driver_api *)dev->api;

	if (api->get_tx_buf == NULL) {
		return NULL;
	}

	return api->get_tx_buf(dev, port);
}

/**
 * @brief Get the length of the received frame
 *
 * This function will return the length of the received frame.
 *
 * @param dev MCB device instance
 * @param port port number
 * @return length of the received frame
 */
__syscall uint16_t mcb_get_rx_len(const struct device *dev, uint8_t port);

static inline uint16_t z_impl_mcb_get_rx_len(const struct device *dev, uint8_t port)
{
	const struct mcb_driver_api *api = (const struct mcb_driver_api *)dev->api;

	if (api->get_rx_len == NULL) {
		return 0;
	}

	return api->get_rx_len(dev, port);
}

/**
 * @brief Get the length of the transmit frame
 *
 * This function will return the length of the transmit frame.
 *
 * @param dev MCB device instance
 * @param port port number
 * @return length of the transmit frame
 */
__syscall uint16_t mcb_get_tx_len(const struct device *dev, uint8_t port);

static inline uint16_t z_impl_mcb_get_tx_len(const struct device *dev, uint8_t port)
{
	const struct mcb_driver_api *api = (const struct mcb_driver_api *)dev->api;

	if (api->get_tx_len == NULL) {
		return 0;
	}

	return api->get_tx_len(dev, port);
}

/**
 * @brief Set the length of the transmit frame
 *
 * This function will set the length of the transmit frame.
 *
 * @param dev MCB device instance
 * @param port port number
 * @param len length of the transmit frame
 */
__syscall void mcb_set_tx_len(const struct device *dev, uint8_t port, uint16_t len);

static inline void z_impl_mcb_set_tx_len(const struct device *dev, uint8_t port, uint16_t len)
{
	const struct mcb_driver_api *api = (const struct mcb_driver_api *)dev->api;

	if (api->set_tx_len == NULL) {
		return;
	}

	api->set_tx_len(dev, port, len);
}

#ifdef __cplusplus
}
#endif

#include <zephyr/syscalls/mcb.h>

#endif /* ZEPHYR_INCLUDE_DRIVERS_MCB_H_ */
