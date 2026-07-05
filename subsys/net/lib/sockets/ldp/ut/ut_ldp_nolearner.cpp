/*
 * SPDX-License-Identifier: Apache-2.0
 * COPYRIGHT (c) 2025 SYSTech Co.
 */
#include <gtest/gtest.h>

#include "ldp.hpp"
#include "ut_header.hpp"

using namespace systech::cif;

using ldp_master_impl = ldp_master<mock_mempool, dummy_cache, std::mutex, std::shared_mutex>;
using ldp_slave_impl = ldp_slave<dummy_cache>;
simu_mcb::buffer_t simu_mcb::rx_buf_[max_sid][max_port];
simu_mcb::buffer_t simu_mcb::tx_buf_[max_sid][max_port];
uint16_t simu_mcb::max_rx_[max_sid][max_port];
uint16_t simu_mcb::rx_len_[max_sid][max_port];
uint16_t simu_mcb::tx_len_[max_sid][max_port];
bool simu_mcb::data_ready_[max_sid][max_port];
bool simu_mcb::port_enabled_[max_sid][max_port];
bool simu_mcb::write_allowed_[max_sid][max_port];
uint32_t simu_mcb::status_[max_sid];
simu_mcb::role simu_mcb::role_[max_sid];
simu_mcb::io_mode simu_mcb::io_mode_[max_sid];
uint32_t simu_mcb::response_delay_us_[max_sid][max_port];
std::list<void *> mock_mempool::ptrs;

namespace {

struct ldp_master_testable: public ldp_master_impl {
	using ldp_master_impl::ldp_master_impl;

	void schedule_bc(unsigned delta_us)
	{
		this->bc_->schedule(delta_us);
	}

	work_queue_if::id first_async_wq_id() const
	{
		return this->async_conns_.front()->wq_id;
	}
};

static simu_work_queue::item &require_async_item(simu_work_queue &wq,
					 const ldp_master_testable &m)
{
	auto *item = wq.find_item(m.first_async_wq_id());
	EXPECT_NE(item, nullptr);
	return *item;
}

static void run_async_round(simu_work_queue &wq, const ldp_master_testable &m)
{
	auto &item = require_async_item(wq, m);
	auto delta = item.time_left > 0 ? static_cast<uint32_t>(item.time_left) : 0u;
	wq.sync(delta);
}

static void queue_slave_payload(ldp_slave_impl &s, int conn, uint8_t sid, uint8_t port,
				const char *payload, uint32_t response_delay_us)
{
	using header = ldp_basic::ldp_a_header;
	auto *tx_hdr = reinterpret_cast<header *>(simu_mcb::tx_buf_[sid][port]);
	auto *rx_hdr = reinterpret_cast<header *>(simu_mcb::rx_buf_[sid][port]);

	rx_hdr->magic = ldp_basic::LDP_MAGIC;
	rx_hdr->xid = tx_hdr->xid;
	rx_hdr->rxid = tx_hdr->xid;
	simu_mcb::rx_len_[sid][port] = sizeof(header);
	simu_mcb::response_delay_us_[sid][port] = response_delay_us;
	ASSERT_EQ(s.send(conn, reinterpret_cast<const uint8_t *>(payload), 8), 8);
}

} // namespace

#define NO_TIMEOUT     0x1000'0000
#define ASYNC_PORT(n)  ((0x08 + (n)) & 0x1F)

TEST(test_ldp_nolearner, useful_progress_keeps_full_cycle_schedule)
{
	simu_work_queue wq;
	simu_mcb bus_m(0), bus_s(1);
	ldp_master_testable m(&bus_m, &wq);
	ldp_slave_impl s(&bus_s);

	ldp_master_async_config m_cfg = {ASYNC_PORT(0), 1, 5000, NO_TIMEOUT, false, false,
					 false};
	ldp_slave_async_config s_cfg = {ASYNC_PORT(0), 32};
	int conn_m = m.create(true, &m_cfg);
	int conn_s = s.create(true, &s_cfg);
	uint8_t rx_buf[32] = {};

	ASSERT_EQ(conn_m, 0);
	ASSERT_EQ(conn_s, 0);

	m.schedule_bc(1000);
	queue_slave_payload(s, conn_s, bus_s.sid_, ASYNC_PORT(0), "rsp:000", 1000);
	run_async_round(wq, m);

	ASSERT_EQ(m.recv(conn_m, rx_buf, sizeof(rx_buf)), 8);
	EXPECT_STREQ(reinterpret_cast<const char *>(rx_buf), "rsp:000");

	auto &item = require_async_item(wq, m);
	EXPECT_EQ(item.last_reset_delay, item.last_reset_cycle);
	EXPECT_EQ(item.last_reset_cycle, 5000u);
}
