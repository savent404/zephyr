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

#include "ldp/inc/ldp.hpp"
#include "ldp_utils_ze.hpp"

namespace systech::cif::zephyr
{
struct mcb_dummy: public mcb_if {
	explicit mcb_dummy(const struct device *dev) : dev_(dev)
	{
	}
	virtual void reset(uint8_t r)
	{
	}

	virtual void set_poll_time(uint16_t timeout)
	{
	}

	virtual void config_port(uint8_t port, bool enable, bool w_allow, uint16_t max_rx)
	{
	}

	virtual uint8_t *get_rx_buf(uint8_t port)
	{
		return dummy_buf;
	}
	virtual uint8_t *get_tx_buf(uint8_t port)
	{
		return dummy_buf;
	}
	virtual uint16_t get_rx_len(uint8_t port)
	{
		return 0;
	}
	virtual uint16_t get_tx_len(uint8_t port)
	{
		return 0;
	}
	virtual void set_tx_len(uint8_t port, uint16_t len)
	{
	}
	virtual uint32_t get_status()
	{
		/* always return T_ERROR and R_ERROR(for test) */
		return MCB_ERR_T_ERR | MCB_ERR_R_ERR;
	}
	virtual void clr_status(uint32_t bits)
	{
	}
	virtual void tx(uint8_t port, uint8_t dst_sid, bool r)
	{
	}
	virtual bool has_rx(uint8_t port)
	{
		return false;
	}
	virtual void clr_rx(uint8_t port)
	{
	}
	virtual uint8_t get_sid()
	{
		return 0;
	}

      private:
	const struct device *dev_;
	uint8_t dummy_buf[2048]; /* make SoC happy, cause nullptr will cause panic */
};

using ldp_master_impl = ldp_master<ldp_mem_slab, ldp_cache_if>;
using ldp_slave_impl = ldp_slave<ldp_cache_if>;
using ldp_mcb_impl = mcb_dummy;
} // namespace systech::cif::zephyr
