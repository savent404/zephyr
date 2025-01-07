/**
 * @file ldp_port_ze.hpp
 * @author Liao YuanKai (savent_gate@outlook.com)
 * @brief LDP port for Zephyr
 * @version 0.1
 * @date 2025-01-05
 *
 * SPDX-License-Identifier: Apache-2.0
 * @copyright Copyright (c) 2025 SYSTech Co.
 *
 */
#pragma once

#include <zephyr/kernel.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/drivers/mcb.h>

#include "ldp/inc/ldp.hpp"
#include "ldp_utils_ze.hpp"

namespace systech::cif::zephyr
{
struct mcb_zephyr: public mcb_if {
	explicit mcb_zephyr(const struct device *dev) : dev_(dev)
	{
	}
	virtual void reset(uint8_t r)
	{
		mcb_reset(dev_, r);
	}

	virtual void set_poll_time(uint16_t timeout)
	{
		mcb_poll_time(dev_, timeout);
	}

	virtual void config_port(uint8_t port, bool enable, bool w_allow, uint16_t max_rx)
	{
		mcb_config_port(dev_, port, enable, w_allow, max_rx);
	}

	virtual uint8_t *get_rx_buf(uint8_t port)
	{
		auto ptr = mcb_get_rx_buf(dev_, port);

		return reinterpret_cast<uint8_t *>(ptr);
	}
	virtual uint8_t *get_tx_buf(uint8_t port)
	{
		auto ptr = mcb_get_tx_buf(dev_, port);

		return reinterpret_cast<uint8_t *>(ptr);
	}
	virtual uint16_t get_rx_len(uint8_t port)
	{
		return mcb_get_rx_len(dev_, port);
	}
	virtual uint16_t get_tx_len(uint8_t port)
	{
		return mcb_get_tx_len(dev_, port);
	}
	virtual void set_tx_len(uint8_t port, uint16_t len)
	{
		mcb_set_tx_len(dev_, port, len);
	}
	virtual uint32_t get_status()
	{
		uint32_t status = 0;

		mcb_get_status(dev_, &status);

		return status;
	}
	virtual void clr_status(uint32_t bits)
	{
		mcb_clr_status(dev_, bits);
	}
	virtual void tx(uint8_t port, uint8_t dst_sid, bool r)
	{
		mcb_tx(dev_, dst_sid, port);
	}
	virtual bool has_rx(uint8_t port)
	{
		return mcb_rx_is_ready(dev_, port) == 1;
	}
	virtual void clr_rx(uint8_t port)
	{
		mcb_rx_clr(dev_, port);
	}

	virtual uint8_t get_sid()
	{
		/* FIXME: get SID from SoC */
		return 0;
	}

      private:
	const struct device *dev_;
};

using ldp_master_impl = ldp_master<ldp_mem_slab, ldp_cache_if>;
using ldp_slave_impl = ldp_slave<ldp_cache_if>;
using ldp_mcb_impl = mcb_zephyr;
} // namespace systech::cif::zephyr
