/**
 * @file ut_ldp_sm.cpp
 * @author Liao YuanKai(savent_gate@outlook.com)
 * @version 0.1
 * @date 2025-01-07
 * @license This project is CLOSED SOURCE, All Rights Reserved
 * @copyright Copyright (c) 2025 SYSTech Co.
 *
 */
#include <assert.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ldp.hpp"
#include "ut_header.hpp"

using namespace ::testing;
using namespace systech::cif;
using err = ldp_basic::ldp_error;

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

using err = ldp_basic::ldp_error;

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

	uint32_t first_async_cycle() const
	{
		return this->async_conns_.front()->cycle;
	}

	work_queue_if::id sync_wq_id() const
	{
		return this->sync_wq_id_;
	}

	work_queue_if::id bc_wq_id() const
	{
		return this->bc_wq_id_;
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

static void expect_master_payload(ldp_master_testable &m, int conn, const char *payload)
{
	uint8_t rx_buf[32] = {};
	ASSERT_EQ(m.recv(conn, rx_buf, sizeof(rx_buf)), 8);
	EXPECT_STREQ(reinterpret_cast<const char *>(rx_buf), payload);
}

static void pump_until_master_payload(simu_work_queue &wq, ldp_master_testable &m, int conn,
			      const char *payload, int expected_len, int max_rounds = 8)
{
	uint8_t rx_buf[32] = {};

	for (int i = 0; i < max_rounds; ++i) {
		m.schedule_bc(1000);
		wq.sync();
		int ret = m.recv(conn, rx_buf, sizeof(rx_buf));
		if (ret == expected_len) {
			ASSERT_STREQ(reinterpret_cast<const char *>(rx_buf), payload);
			return;
		}
		ASSERT_EQ(ret, -err::LDP_ERR_AGAIN);
	}

	FAIL() << "timed out waiting for master payload: " << payload;
}

static void pump_until_slave_payload(simu_work_queue &wq, ldp_master_testable &m, ldp_slave_impl &s,
			     int conn, const char *payload, int expected_len,
			     int max_rounds = 8)
{
	uint8_t rx_buf[32] = {};

	for (int i = 0; i < max_rounds; ++i) {
		m.schedule_bc(1000);
		wq.sync();
		int ret = s.recv(conn, rx_buf, sizeof(rx_buf));
		if (ret == expected_len) {
			ASSERT_STREQ(reinterpret_cast<const char *>(rx_buf), payload);
			return;
		}
		ASSERT_EQ(ret, -err::LDP_ERR_AGAIN);
	}

	FAIL() << "timed out waiting for slave payload: " << payload;
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
#define SYNC_PORT(n)   ((0x00 + (n)) & 7)
#define ASYNC_PORT(n)  ((0x08 + (n)) & 0x1F)

TEST_F(test_ldp_sm, basic_concept)
{
	simu_work_queue wq;
	simu_mcb bus_m(0), bus_s1(1), bus_s2(2);
	ldp_master_testable m(&bus_m, &wq);
	ldp_slave_impl s1(&bus_s1), s2(&bus_s2);

	ldp_master_async_config m_cfg_cfg[2] = {
		{ASYNC_PORT(0), 1, 1000, NO_TIMEOUT, false, true, true},
		{ASYNC_PORT(0), 2, 1000, NO_TIMEOUT, false, true, true},
	};
	ldp_master_sync_config m_cfg_io[2] = {
		{SYNC_PORT(0), 1, 1000, NO_TIMEOUT, false, false},
		{SYNC_PORT(0), 2, 1000, NO_TIMEOUT, false, false},
	};
	ldp_master_async_config m_cfg_async_io[2] = {
		{ASYNC_PORT(1), 1, 1000, NO_TIMEOUT, false, false, false},
		{ASYNC_PORT(1), 2, 1000, NO_TIMEOUT, false, false, false},
	};

	ldp_slave_async_config s_cfg_cfg[2] = {
		{ASYNC_PORT(0), 32},
		{ASYNC_PORT(0), 8},
	};
	ldp_slave_sync_config s_cfg_io[2] = {
		{SYNC_PORT(0), 32, true},
		{SYNC_PORT(0), 8, true},
	};
	ldp_slave_async_config s_cfg_async_io[2] = {
		{ASYNC_PORT(1), 32},
		{ASYNC_PORT(1), 8},
	};

	int m_conn_cfg[2], m_conn_io[2], m_conn_async_io[2];
	int s_conn_cfg[2], s_conn_io[2], s_conn_async_io[2];

	/* Create master and slave connection */
	s_conn_cfg[0] = s1.create(true, &s_cfg_cfg[0]);
	s_conn_cfg[1] = s2.create(true, &s_cfg_cfg[1]);
	s_conn_io[0] = s1.create(false, &s_cfg_io[0]);
	s_conn_io[1] = s2.create(false, &s_cfg_io[1]);
	s_conn_async_io[0] = s1.create(true, &s_cfg_async_io[0]);
	s_conn_async_io[1] = s2.create(true, &s_cfg_async_io[1]);

	m_conn_cfg[0] = m.create(true, &m_cfg_cfg[0]);
	m_conn_cfg[1] = m.create(true, &m_cfg_cfg[1]);

	ASSERT_EQ(s_conn_cfg[0], 0);
	ASSERT_EQ(s_conn_io[0], 1);
	ASSERT_EQ(s_conn_cfg[1], 0);
	ASSERT_EQ(s_conn_io[1], 1);
	ASSERT_EQ(s_conn_async_io[0], 2);
	ASSERT_EQ(s_conn_async_io[1], 2);
	ASSERT_EQ(m_conn_cfg[0], 0);
	ASSERT_EQ(m_conn_cfg[1], 1);

	/* Configure slave port */
	uint8_t rx_buf[32];
	int ret;

	{
		/* First round, slave get the request and response with empty frame */
		ret = m.send(m_conn_cfg[0], (const uint8_t *)"cfg:000", 8);
		ASSERT_EQ(ret, 8);
		ret = m.send(m_conn_cfg[1], (const uint8_t *)"cfg:001", 8);
		ASSERT_EQ(ret, 8);

		m.schedule_bc(1000);
		wq.sync();

		ret = s1.recv(s_conn_cfg[0], rx_buf, 32);
		ASSERT_EQ(ret, 8);
		EXPECT_STREQ((const char *)rx_buf, "cfg:000");

		ret = s2.recv(s_conn_cfg[1], rx_buf, 32);
		ASSERT_EQ(ret, 8);
		EXPECT_STREQ((const char *)rx_buf, "cfg:001");

		ret = s1.send(s_conn_cfg[0], (const uint8_t *)"cfg>000", 8);
		ASSERT_EQ(ret, 8);

		ret = s2.send(s_conn_cfg[1], (const uint8_t *)"cfg>001", 8);
		ASSERT_EQ(ret, 8);

		ret = m.recv(m_conn_cfg[0], rx_buf, 32);
		ASSERT_EQ(ret, -err::LDP_ERR_AGAIN);

		ret = m.recv(m_conn_cfg[1], rx_buf, 32);
		ASSERT_EQ(ret, -err::LDP_ERR_AGAIN);
	}

	{
		/* second round, master using empty request to get the configure response */
		m.schedule_bc(1000);
		wq.sync();

		ret = m.recv(m_conn_cfg[0], rx_buf, 32);
		ASSERT_EQ(ret, 8);
		EXPECT_STREQ((const char *)rx_buf, "cfg>000");

		ret = m.recv(m_conn_cfg[1], rx_buf, 32);
		ASSERT_EQ(ret, 8);
		EXPECT_STREQ((const char *)rx_buf, "cfg>001");

		ret = s1.recv(s_conn_cfg[0], rx_buf, 32);
		ASSERT_EQ(ret, -err::LDP_ERR_AGAIN);

		ret = s2.recv(s_conn_cfg[1], rx_buf, 32);
		ASSERT_EQ(ret, -err::LDP_ERR_AGAIN);
	}

	{
		/* third round, no more new data needs to be processed */
		m.schedule_bc(1000);
		wq.sync();

		ret = m.recv(m_conn_cfg[0], rx_buf, 32);
		ASSERT_EQ(ret, -err::LDP_ERR_AGAIN);

		ret = m.recv(m_conn_cfg[1], rx_buf, 32);
		ASSERT_EQ(ret, -err::LDP_ERR_AGAIN);

		ret = s1.recv(s_conn_cfg[0], rx_buf, 32);
		ASSERT_EQ(ret, -err::LDP_ERR_AGAIN);

		ret = s2.recv(s_conn_cfg[1], rx_buf, 32);
		ASSERT_EQ(ret, -err::LDP_ERR_AGAIN);
	}

	/* Now we can start io test */
	m_conn_io[0] = m.create(false, &m_cfg_io[0]);
	m_conn_io[1] = m.create(false, &m_cfg_io[1]);
	m_conn_async_io[0] = m.create(true, &m_cfg_async_io[0]);
	m_conn_async_io[1] = m.create(true, &m_cfg_async_io[1]);
	ASSERT_EQ(m_conn_io[0], 2);
	ASSERT_EQ(m_conn_io[1], 3);
	ASSERT_EQ(m_conn_async_io[0], 4);
	ASSERT_EQ(m_conn_async_io[1], 5);

	{
		/* IO SYNC setup
		 * s1--0x40->  m: io>000
		 * s2--0x40->  m: io>001
		 * s1--0x60->  m: aio>000
		 *  m--0x60-> s2: aio:001
		 *  m--0x40-> s1: io:000
		 */
		ret = s1.send(s_conn_io[0], (const uint8_t *)"io>000", 8);
		ASSERT_EQ(ret, 8);

		ret = s2.send(s_conn_io[1], (const uint8_t *)"io>001", 8);
		ASSERT_EQ(ret, 8);

		ret = s1.send(s_conn_async_io[0], (const uint8_t *)"aio>000", 8);
		ASSERT_EQ(ret, 8);

		ret = m.send(m_conn_async_io[1], (const uint8_t *)"aio:001", 8);
		ASSERT_EQ(ret, 8);

		ret = m.send(m_conn_io[0], (const uint8_t *)"io:000", 8);
		ASSERT_EQ(ret, 8);

		m.schedule_bc(1000);
		wq.sync();

		ret = m.recv(m_conn_io[0], rx_buf, 32);
		ASSERT_EQ(ret, 8);
		EXPECT_STREQ((const char *)rx_buf, "io>000");

		ret = m.recv(m_conn_io[1], rx_buf, 32);
		ASSERT_EQ(ret, 8);
		EXPECT_STREQ((const char *)rx_buf, "io>001");

		ret = m.recv(m_conn_async_io[0], rx_buf, 32);
		ASSERT_EQ(ret, 8);
		EXPECT_STREQ((const char *)rx_buf, "aio>000");

		ret = s1.recv(s_conn_io[0], rx_buf, 32);
		ASSERT_EQ(ret, 8);
		EXPECT_STREQ((const char *)rx_buf, "io:000");

		ret = s2.recv(s_conn_io[1], rx_buf, 32);
		ASSERT_EQ(ret, -err::LDP_ERR_AGAIN);

		ret = s1.recv(s_conn_async_io[0], rx_buf, 32);
		ASSERT_EQ(ret, -err::LDP_ERR_AGAIN);

		ret = s2.recv(s_conn_async_io[1], rx_buf, 32);
		ASSERT_EQ(ret, 8);
		EXPECT_STREQ((const char *)rx_buf, "aio:001");

		ret = m.recv(m_conn_async_io[1], rx_buf, 32);
		ASSERT_EQ(ret, -err::LDP_ERR_AGAIN);
	}
}

TEST_F(test_ldp_sm, master_rejects_ports_outside_mcb_range)
{
	simu_work_queue wq;
	simu_mcb bus_m(0);
	ldp_master_impl m(&bus_m, &wq);
	ldp_master_sync_config cfg = {
		33, 1, 1000, NO_TIMEOUT, false, false,
	};

	int conn = m.create(false, &cfg);

	EXPECT_EQ(conn, -err::LDP_ERR_INVALID);
}

TEST_F(test_ldp_sm, DISABLED_harq)
{
	simu_work_queue wq;
	simu_mcb bus_m(0), bus_s1(1), bus_s2(2);
	ldp_master_testable m(&bus_m, &wq);
	ldp_slave_impl s1(&bus_s1), s2(&bus_s2);

	ldp_master_async_config m_cfg_cfg[] = {
		{ASYNC_PORT(0), 1, 1000, 10000, false, false, false},
	};
	ldp_slave_async_config s_cfg_cfg[] = {
		{ASYNC_PORT(0), 32},
	};
	int m_conn = m.create(true, &m_cfg_cfg[0]);
	int s_conn = s1.create(true, &s_cfg_cfg[0]);
	const int max_round = ldp_master_impl::LDP_MAX_HARQ;

	ASSERT_EQ(m_conn, 0);
	ASSERT_EQ(s_conn, 0);

	/* FIXME: Since can't split async_handler into single round, we can't test
	 * the harq feature */

	/* Make sure every req-res has new data to process */
	uint8_t tx_m_buf[32], tx_s_buf[32], rx_buf[32];

	memcpy(tx_m_buf, "cfg:000", 8);
	memcpy(tx_s_buf, "cfg>000", 8);

	wq.sync();
	for (int i = 0; i < max_round; i++) {
		tx_m_buf[4] = '0' + i;
		tx_s_buf[4] = '0' + i;
		ASSERT_EQ(m.send(m_conn, tx_m_buf, 8), 8);
		// ASSERT_EQ(s1.send(s_conn, tx_s_buf, 8), 8);
	}

	for (int i = 0; i < max_round; i++) {
		tx_m_buf[4] = '0' + i;
		tx_s_buf[4] = '0' + i;
		wq.sync();
		printf("Round %d\n", i);
		// ASSERT_EQ(m.recv(m_conn, rx_buf, 32), 8);
		// EXPECT_STREQ((const char *)rx_buf, (const char *)tx_s_buf);
		ASSERT_EQ(s1.recv(s_conn, rx_buf, 32), 8);
		EXPECT_STREQ((const char *)rx_buf, (const char *)tx_m_buf);
	}
}

TEST_F(test_ldp_sm, sync_timeout)
{
	simu_work_queue wq;
	simu_mcb bus_m(0), bus_s1(1);
	ldp_master_testable m(&bus_m, &wq);
	ldp_slave_impl s1(&bus_s1);

	ldp_master_sync_config m_cfg_cfg[] = {
		{SYNC_PORT(0), 1, 1000, 3000, false, false},
	};
	ldp_slave_sync_config s_cfg_cfg[] = {
		{SYNC_PORT(0), 32, true},
	};
	uint8_t rx_buf[32];

	int m_conn = m.create(false, &m_cfg_cfg[0]);
	int s_conn = s1.create(false, &s_cfg_cfg[0]);

	ASSERT_EQ(m_conn, 0);
	ASSERT_EQ(s_conn, 0);

	int ret = 0;

	for (int i = 0; i < 4; i++) {
		ret = m.send(m_conn, (const uint8_t *)"io:000", 8);
		ASSERT_EQ(ret, 8);

		wq.sync();

		ret = m.recv(m_conn, rx_buf, 32);

		if (i < 2) {
			ASSERT_EQ(ret, -err::LDP_ERR_AGAIN);
		} else {
			ASSERT_EQ(ret, -err::LDP_ERR_ATIMEOUT);
		}
	}
}

TEST_F(test_ldp_sm, async_timeout)
{
	simu_work_queue wq;
	simu_mcb bus_m(0), bus_s1(1);
	ldp_master_testable m(&bus_m, &wq);
	ldp_slave_impl s1(&bus_s1);

	ldp_master_async_config m_cfg_cfg[] = {
		{ASYNC_PORT(0), 1, 1000, 3000, false, true, true},
		{ASYNC_PORT(1), 1, 1000, 3000, false, false, false},
	};
	ldp_slave_async_config s_cfg_cfg[] = {
		{ASYNC_PORT(0), 32},
		{ASYNC_PORT(1), 32},
	};
	uint8_t rx_buf[32];

	int m_conn[] = {m.create(true, &m_cfg_cfg[0]), m.create(true, &m_cfg_cfg[1])};
	int s_conn[] = {s1.create(true, &s_cfg_cfg[0]), s1.create(true, &s_cfg_cfg[1])};

	ASSERT_EQ(m_conn[0], 0);
	ASSERT_EQ(m_conn[1], 1);
	ASSERT_EQ(s_conn[0], 0);
	ASSERT_EQ(s_conn[1], 1);

	/* Case 1: slave no response, master async timeout */
	m.send(m_conn[0], (const uint8_t *)"cfg:000", 8);
	m.send(m_conn[1], (const uint8_t *)"aio:000", 8);

	int ret0 = -err::LDP_ERR_AGAIN;
	int ret1 = -err::LDP_ERR_AGAIN;
	for (int i = 0; i < 8; i++) {
		m.schedule_bc(1000);
		wq.sync();
		ret0 = m.recv(m_conn[0], rx_buf, 32);
		ret1 = m.recv(m_conn[1], rx_buf, 32);
		if (ret0 == -err::LDP_ERR_ATIMEOUT && ret1 == -err::LDP_ERR_ATIMEOUT) {
			break;
		}
		ASSERT_EQ(ret0, -err::LDP_ERR_AGAIN);
		ASSERT_EQ(ret1, -err::LDP_ERR_AGAIN);
	}
	ASSERT_EQ(ret0, -err::LDP_ERR_ATIMEOUT);
	ASSERT_EQ(ret1, -err::LDP_ERR_ATIMEOUT);

	m.schedule_bc(1000);
	wq.sync();
	ASSERT_EQ(m.send(m_conn[0], (const uint8_t *)"cfg:001", 8), -err::LDP_ERR_ATIMEOUT);
	ASSERT_EQ(m.send(m_conn[1], (const uint8_t *)"cfg:001", 8), -err::LDP_ERR_ATIMEOUT);
	ASSERT_EQ(m.recv(m_conn[0], rx_buf, 32), -err::LDP_ERR_ATIMEOUT);
	ASSERT_EQ(m.recv(m_conn[1], rx_buf, 32), -err::LDP_ERR_ATIMEOUT);

	/* Case 2: slave start to response, master async timeout cleared automatically */
	ASSERT_EQ(s1.recv(s_conn[0], rx_buf, 32), 8);
	ASSERT_EQ(s1.recv(s_conn[1], rx_buf, 32), 8);
	wq.sync();
	m.send(m_conn[0], (const uint8_t *)"cfg:000", 8);
	m.send(m_conn[1], (const uint8_t *)"aio:000", 8);
	ASSERT_EQ(m.recv(m_conn[0], rx_buf, 32), -err::LDP_ERR_AGAIN);
	ASSERT_EQ(m.recv(m_conn[1], rx_buf, 32), -err::LDP_ERR_AGAIN);
}

TEST_F(test_ldp_sm, worst_case_slave_no_response)
{
	simu_work_queue wq;
	simu_mcb bus_m(0), bus_s1(1), bus_s2(2);
	ldp_master_testable m(&bus_m, &wq);
	ldp_slave_impl s1(&bus_s1), s2(&bus_s2);

	ldp_master_async_config m_cfg_cfg[2] = {
		{ASYNC_PORT(0), 1, 1000, 10000, false, true, true},
		{ASYNC_PORT(0), 2, 1000, 10000, false, true, true},
	};
	ldp_master_sync_config m_cfg_io[2] = {
		{SYNC_PORT(0), 1, 1000, 1000, false, false},
		{SYNC_PORT(0), 2, 1000, 1000, false, false},
	};
	ldp_master_async_config m_cfg_async_io[2] = {
		{ASYNC_PORT(1), 1, 1000, 2000, false, false, false},
		{ASYNC_PORT(1), 2, 1000, 2000, false, false, false},
	};

	ldp_slave_async_config s_cfg_cfg[2] = {
		{ASYNC_PORT(0), 32},
		{ASYNC_PORT(0), 8},
	};
	ldp_slave_sync_config s_cfg_io[2] = {
		{SYNC_PORT(0), 32, true},
		{SYNC_PORT(0), 8, true},
	};
	ldp_slave_async_config s_cfg_async_io[2] = {
		{ASYNC_PORT(1), 32},
		{ASYNC_PORT(1), 8},
	};

	int m_conn_cfg[2], m_conn_io[2], m_conn_async_io[2];
	int s_conn_cfg[2], s_conn_io[2], s_conn_async_io[2];

	s_conn_cfg[0] = s1.create(true, &s_cfg_cfg[0]);
	/* s_conn_cfg[1] = s2.create(true, &s_cfg_cfg[1]); */
	s_conn_io[0] = s1.create(false, &s_cfg_io[0]);
	/* s_conn_io[1] = s2.create(false, &s_cfg_io[1]); */
	s_conn_async_io[0] = s1.create(true, &s_cfg_async_io[0]);
	s_conn_async_io[1] = s2.create(true, &s_cfg_async_io[1]);

	m_conn_cfg[0] = m.create(true, &m_cfg_cfg[0]);
	m_conn_cfg[1] = m.create(true, &m_cfg_cfg[1]);
	m_conn_io[0] = m.create(false, &m_cfg_io[0]);
	m_conn_io[1] = m.create(false, &m_cfg_io[1]);
	m_conn_async_io[0] = m.create(true, &m_cfg_async_io[0]);
	m_conn_async_io[1] = m.create(true, &m_cfg_async_io[1]);

	ASSERT_EQ(s_conn_cfg[0], 0);
	ASSERT_EQ(s_conn_io[0], 1);
	ASSERT_EQ(s_conn_async_io[0], 2);
	ASSERT_EQ(s_conn_async_io[1], 0);
	ASSERT_EQ(m_conn_cfg[0], 0);
	ASSERT_EQ(m_conn_cfg[1], 1);
	ASSERT_EQ(m_conn_io[0], 2);
	ASSERT_EQ(m_conn_io[1], 3);
	ASSERT_EQ(m_conn_async_io[0], 4);
	ASSERT_EQ(m_conn_async_io[1], 5);

	uint8_t rx_buf[32];
	int ret;
	{
		/* Not opened port shall reject the request */
		m.send(m_conn_cfg[0], (const uint8_t *)"cfg:000", 8);
		m.send(m_conn_cfg[1], (const uint8_t *)"cfg:001", 8);
		m.send(m_conn_io[0], (const uint8_t *)"io:000", 8);
		m.send(m_conn_io[1], (const uint8_t *)"io:001", 8);
		m.send(m_conn_async_io[0], (const uint8_t *)"aio:000", 8);
		m.send(m_conn_async_io[1], (const uint8_t *)"aio:001", 8);
		s1.send(s_conn_cfg[0], (const uint8_t *)"cfg>000", 8);
		s1.send(s_conn_io[0], (const uint8_t *)"io>000", 8);
		s2.send(s_conn_async_io[1], (const uint8_t *)"aio>001", 8);
		m.schedule_bc(1000);
		wq.sync();
		/* NOTE: master will drop this response since strong order is enabled */
		ASSERT_EQ(m.recv(m_conn_cfg[0], rx_buf, 32), -err::LDP_ERR_AGAIN);
		ASSERT_EQ(m.recv(m_conn_cfg[1], rx_buf, 32), -err::LDP_ERR_P_ERROR);
		ASSERT_EQ(m.recv(m_conn_io[0], rx_buf, 32), 8);
		ASSERT_EQ(m.recv(m_conn_io[1], rx_buf, 32), -err::LDP_ERR_ATIMEOUT);
		ASSERT_EQ(m.recv(m_conn_async_io[0], rx_buf, 32), -err::LDP_ERR_AGAIN);
		ASSERT_EQ(m.recv(m_conn_async_io[1], rx_buf, 32), 8);
	}

	{
		/* Master will drop the response if the response is not updated by slave */
		m.send(m_conn_cfg[0], (const uint8_t *)"cfg:000", 8);
		m.schedule_bc(1000);
		wq.sync();
		ASSERT_EQ(m.recv(m_conn_cfg[0], rx_buf, 32), -err::LDP_ERR_AGAIN);

		m.schedule_bc(1000);
		wq.sync();
		ASSERT_EQ(m.recv(m_conn_cfg[0], rx_buf, 32), -err::LDP_ERR_AGAIN);

		s1.recv(s_conn_cfg[0], rx_buf, 32);
		ASSERT_EQ(s1.send(s_conn_cfg[0], (const uint8_t *)"cfg>000", 8), 8);
		m.schedule_bc(1000);
		wq.sync();
		ASSERT_EQ(m.recv(m_conn_cfg[0], rx_buf, 32), 8);
		m.schedule_bc(1000);
		wq.sync();
	}
}

TEST_F(test_ldp_sm, master_slave_switch)
{
	simu_work_queue wq;
	simu_mcb bus_mpu(0), bus_mpu_bak(1), bus_io(2);

	auto mpu = std::make_unique<ldp_master_testable>(&bus_mpu, &wq);
	std::unique_ptr<ldp_slave_impl> mpu_s = nullptr;
	std::unique_ptr<ldp_master_testable> s1_m = nullptr;
	auto s1 = std::make_unique<ldp_slave_impl>(&bus_mpu_bak);
	auto s2 = std::make_unique<ldp_slave_impl>(&bus_io);

	int mpu_conn[6], mpu_bak_conn[6], io_conn[3];

	/* case 1: MPU switch to master, mpu_bak switch to slave */
	{
		const ldp_master_async_config m_cfg_cfg[] = {
			{ASYNC_PORT(0), 1, 1000, 10000, false, true, true},
			{ASYNC_PORT(2), 1, 1000, 10000, false, false, false},
			{ASYNC_PORT(0), 2, 1000, 10000, false, true, true},
			{ASYNC_PORT(2), 2, 1000, 10000, false, false, false},
		};
		const ldp_master_sync_config m_cfg_io[] = {
			{SYNC_PORT(0), 1, 1000, 1000, false, false},
			{SYNC_PORT(0), 2, 1000, 1000, false, false},
		};
		mpu_conn[0] = mpu->create(true, &m_cfg_cfg[0]);
		mpu_conn[1] = mpu->create(true, &m_cfg_cfg[1]);
		mpu_conn[2] = mpu->create(true, &m_cfg_cfg[2]);
		mpu_conn[3] = mpu->create(true, &m_cfg_cfg[3]);
		mpu_conn[4] = mpu->create(false, &m_cfg_io[0]);
		mpu_conn[5] = mpu->create(false, &m_cfg_io[1]);

		ldp_slave_async_config s_cfg_cfg[] = {
			{ASYNC_PORT(0), 32},
			{ASYNC_PORT(2), 32},
		};
		ldp_slave_sync_config s_cfg_io[] = {
			{SYNC_PORT(0), 32, true},
		};
		mpu_bak_conn[0] = s1->create(true, &s_cfg_cfg[0]);
		mpu_bak_conn[1] = s1->create(true, &s_cfg_cfg[1]);
		mpu_bak_conn[2] = s1->create(false, &s_cfg_io[0]);

		io_conn[0] = s2->create(true, &s_cfg_cfg[0]);
		io_conn[1] = s2->create(true, &s_cfg_cfg[1]);
		io_conn[2] = s2->create(false, &s_cfg_io[0]);

		wq.sync();
	}

	/* case 2: backup MPU want to switch master */
	{
		s1->destroy(mpu_bak_conn[0]);
		s1->destroy(mpu_bak_conn[1]);
		s1->destroy(mpu_bak_conn[2]);
		s1 = nullptr;

		s1_m = std::make_unique<ldp_master_testable>(&bus_mpu_bak, &wq);
		ldp_master_sync_config cfg_io[] = {
			{SYNC_PORT(0), 0, 1000, 1000, true, false},
		};
		mpu_bak_conn[0] = s1_m->create(false, &cfg_io[0]);
		s1_m->send(mpu_bak_conn[0], (const uint8_t *)"io>000", 8);
		wq.sync();
		/* NOTE: Due to the execute order, mpu will recv the preempt flag at the second
		 * round in the worst case */
		wq.sync();
	}

	/* case 3: mpu recv R flag, switch to slave */
	{
		uint8_t rx_buf[32];
		ASSERT_EQ(mpu->recv(mpu_conn[4], rx_buf, 32), 8);

		/* NOTE: any port could recv the preempt flag. It is only about the
		 * order of the execution */
		uint32_t extra_err = 0;
		for (int i = 0; i < 5; i++) {
			uint32_t m = mpu->get_extra_error(mpu_conn[i]);
			extra_err |= m;
			mpu->clr_extra_error(mpu_conn[i], m);
		}
		ASSERT_TRUE(extra_err &
			    (1 << err::LDP_ERR_PREEMPT | 1 << err::LDP_ERR_PREV_PREEMPT));
		mpu->destroy(mpu_conn[0]);
		mpu->destroy(mpu_conn[1]);
		mpu->destroy(mpu_conn[2]);
		mpu->destroy(mpu_conn[3]);
		mpu->destroy(mpu_conn[4]);
		mpu->destroy(mpu_conn[5]);
		mpu = nullptr;
	}

	/* case 4: mpu switch to slave mode, establish connection with mpu_bak */
	{
		mpu_s = std::make_unique<ldp_slave_impl>(&bus_mpu);

		ldp_slave_async_config s_cfg_cfg[] = {
			{ASYNC_PORT(0), 32},
			{ASYNC_PORT(2), 32},
		};
		ldp_slave_sync_config s_cfg_io[] = {
			{SYNC_PORT(0), 32, true},
		};

		mpu_conn[0] = mpu_s->create(true, &s_cfg_cfg[0]);
		mpu_conn[1] = mpu_s->create(true, &s_cfg_cfg[1]);
		mpu_conn[2] = mpu_s->create(false, &s_cfg_io[0]);

		ASSERT_EQ(mpu_conn[0], 0);
		ASSERT_EQ(mpu_conn[1], 1);
		ASSERT_EQ(mpu_conn[2], 2);

		mpu_s->send(mpu_conn[0], (const uint8_t *)"cfg>000", 8);
		mpu_s->send(mpu_conn[1], (const uint8_t *)"aio>000", 8);
		mpu_s->send(mpu_conn[2], (const uint8_t *)"io>000", 8);

		wq.sync();

		uint8_t rx_buf[32];
		ASSERT_EQ(s1_m->recv(mpu_bak_conn[0], rx_buf, 32), 8);
		ASSERT_STREQ((const char *)rx_buf, "io>000");
	}
	/* NOTE: Now mpu_bak shall disable its preempt flag, connection back to normal */
	{
		s1_m->destroy(mpu_bak_conn[0]);
		ldp_master_sync_config cfg_io[] = {
			{SYNC_PORT(0), 0, 1000, 1000, true, false},
		};
		mpu_bak_conn[0] = s1_m->create(false, &cfg_io[0]);
	}
}

TEST_F(test_ldp_sm, async_recv_memleak)
{
	simu_work_queue wq;
	simu_mcb bus_mpu(0), bus_mpu_bak(1), bus_io(2);

	auto mpu = std::make_unique<ldp_master_testable>(&bus_mpu, &wq);
	auto s1 = std::make_unique<ldp_slave_impl>(&bus_mpu_bak);
	auto s2 = std::make_unique<ldp_slave_impl>(&bus_io);

	int mpu_conn[2];
	int s_conn[2];

	ldp_master_async_config m_cfg_cfg[] = {
		{ASYNC_PORT(2), 1, 1000, 10000, false, false, false},
		{ASYNC_PORT(2), 2, 1000, 10000, false, false, false},
	};
	ldp_slave_async_config s_cfg_cfg[] = {
		{ASYNC_PORT(2), 32},
		{ASYNC_PORT(2), 32},
	};

	mpu_conn[0] = mpu->create(true, &m_cfg_cfg[0]);
	mpu_conn[1] = mpu->create(true, &m_cfg_cfg[1]);
	s_conn[0] = s1->create(true, &s_cfg_cfg[0]);
	s_conn[1] = s2->create(true, &s_cfg_cfg[1]);

	ASSERT_EQ(mpu_conn[0], 0);
	ASSERT_EQ(mpu_conn[1], 1);
	ASSERT_EQ(s_conn[0], 0);
	ASSERT_EQ(s_conn[1], 0);

	/* Update slave's send buffer size, check memleak issue */
	uint8_t rx_buf[32];
	mpu->send(mpu_conn[0], (const uint8_t *)"aio:000", 8);
	mpu->send(mpu_conn[1], (const uint8_t *)"aio:001", 8);
	s1->send(s_conn[0], (const uint8_t *)"res>008\0", 8);
	s2->send(s_conn[1], (const uint8_t *)"res>00C\0", 12);
	mpu->schedule_bc(1000);
	wq.sync();
	ASSERT_EQ(mpu->recv(mpu_conn[0], rx_buf, 32), 8);
	ASSERT_STREQ((const char *)rx_buf, "res>008");
	ASSERT_EQ(mpu->recv(mpu_conn[1], rx_buf, 32), 12);
	ASSERT_STREQ((const char *)rx_buf, "res>00C");
	ASSERT_EQ(s1->recv(s_conn[0], rx_buf, 32), 8);
	ASSERT_STREQ((const char *)rx_buf, "aio:000");
	ASSERT_EQ(s2->recv(s_conn[1], rx_buf, 32), 8);
	ASSERT_STREQ((const char *)rx_buf, "aio:001");

	mpu->send(mpu_conn[0], (const uint8_t *)"aio:002", 8);
	mpu->send(mpu_conn[1], (const uint8_t *)"aio:003", 8);
	pump_until_slave_payload(wq, *mpu, *s1, s_conn[0], "aio:002", 8);
	pump_until_slave_payload(wq, *mpu, *s2, s_conn[1], "aio:003", 8);
	ASSERT_EQ(s1->send(s_conn[0], (const uint8_t *)"res>00C\0", 12), 12);
	ASSERT_EQ(s2->send(s_conn[1], (const uint8_t *)"res>010\0", 16), 16);
	pump_until_master_payload(wq, *mpu, mpu_conn[0], "res>00C", 12);
	pump_until_master_payload(wq, *mpu, mpu_conn[1], "res>010", 16);
}

TEST_F(test_ldp_sm, sync_send_memleak)
{
	simu_work_queue wq;
	simu_mcb bus_mpu(0), bus_mpu_bak(1), bus_io(2);

	auto mpu = std::make_unique<ldp_master_testable>(&bus_mpu, &wq);
	auto s1 = std::make_unique<ldp_slave_impl>(&bus_mpu_bak);
	auto s2 = std::make_unique<ldp_slave_impl>(&bus_io);

	int mpu_conn[2];
	int s_conn[2];

	ldp_master_sync_config m_cfg_io[] = {
		{SYNC_PORT(0), 1, 1000, 1000, true, false},
		{SYNC_PORT(0), 2, 1000, 1000, true, false},
	};

	ldp_slave_sync_config s_cfg_io[] = {
		{SYNC_PORT(0), 32, true},
		{SYNC_PORT(0), 32, true},
	};

	mpu_conn[0] = mpu->create(false, &m_cfg_io[0]);
	mpu_conn[1] = mpu->create(false, &m_cfg_io[1]);
	s_conn[0] = s1->create(false, &s_cfg_io[0]);
	s_conn[1] = s2->create(false, &s_cfg_io[1]);

	ASSERT_EQ(mpu_conn[0], 0);
	ASSERT_EQ(mpu_conn[1], 1);
	ASSERT_EQ(s_conn[0], 0);
	ASSERT_EQ(s_conn[1], 0);

	/* Update send buffer size, check memleak issue */
	uint8_t rx_buf[32];
	mpu->send(mpu_conn[0], (const uint8_t *)"io:000", 8);
	mpu->send(mpu_conn[1], (const uint8_t *)"io:001", 8);
	s1->send(s_conn[0], (const uint8_t *)"io>008\0", 8);
	s2->send(s_conn[1], (const uint8_t *)"io>00C\0", 12);
	wq.sync();
	ASSERT_EQ(mpu->recv(mpu_conn[0], rx_buf, 32), 8);
	ASSERT_STREQ((const char *)rx_buf, "io>008");
	ASSERT_EQ(mpu->recv(mpu_conn[1], rx_buf, 32), 12);
	ASSERT_STREQ((const char *)rx_buf, "io>00C");
	ASSERT_EQ(s1->recv(s_conn[0], rx_buf, 32), 8);
	ASSERT_STREQ((const char *)rx_buf, "io:000");
	ASSERT_EQ(s2->recv(s_conn[1], rx_buf, 32), 8);
	ASSERT_STREQ((const char *)rx_buf, "io:001");

	mpu->send(mpu_conn[0], (const uint8_t *)"io:002", 8);
	mpu->send(mpu_conn[1], (const uint8_t *)"io:003", 8);
	s1->send(s_conn[0], (const uint8_t *)"io>00C\0", 12);
	s2->send(s_conn[1], (const uint8_t *)"io>010\0", 16);
	wq.sync();
	ASSERT_EQ(mpu->recv(mpu_conn[0], rx_buf, 32), 12);
	ASSERT_STREQ((const char *)rx_buf, "io>00C");
	ASSERT_EQ(mpu->recv(mpu_conn[1], rx_buf, 32), 16);
	ASSERT_STREQ((const char *)rx_buf, "io>010");
	ASSERT_EQ(s1->recv(s_conn[0], rx_buf, 32), 8);
	ASSERT_STREQ((const char *)rx_buf, "io:002");
	ASSERT_EQ(s2->recv(s_conn[1], rx_buf, 32), 8);
	ASSERT_STREQ((const char *)rx_buf, "io:003");

	mpu->send(mpu_conn[0], (const uint8_t *)"io0", 4);
	mpu->send(mpu_conn[1], (const uint8_t *)"io1", 4);
	s1->send(s_conn[0], (const uint8_t *)"io0", 4);
	s2->send(s_conn[1], (const uint8_t *)"io1", 4);
	wq.sync();
	ASSERT_EQ(mpu->recv(mpu_conn[0], rx_buf, 32), 4);
	ASSERT_STREQ((const char *)rx_buf, "io0");
	ASSERT_EQ(mpu->recv(mpu_conn[1], rx_buf, 32), 4);
	ASSERT_STREQ((const char *)rx_buf, "io1");
	ASSERT_EQ(s1->recv(s_conn[0], rx_buf, 32), 4);
	ASSERT_STREQ((const char *)rx_buf, "io0");
}

TEST_F(test_ldp_sm, async_bandwidth_control)
{
	simu_work_queue wq;
	simu_mcb bus_m(0, simu_mcb::ps_io, 10), bus_s1(1), bus_s2(2);
	ldp_master_testable m(&bus_m, &wq);
	ldp_slave_impl s1(&bus_s1), s2(&bus_s2);

	ASSERT_EQ(int(m.bc_ratio_sync * 10), 3); /* max 3 pps for sync */
	ASSERT_EQ(int(m.bc_ratio_async * 10), 2); /* at least 2 pps for async */

	int mpu_conn[4], s_conn[4];

	ldp_master_sync_config m_cfg_io[] = {
		{SYNC_PORT(0), 1, 1'000'000, 1'000'000, false, false},
		{SYNC_PORT(0), 2, 1'000, 1'000, false, false},
	};
	ldp_master_async_config m_cfg_async_io[] = {
		{ASYNC_PORT(2), 1, 1'000'000, 1'000'000'000, false, false, false, false, 100},
		{ASYNC_PORT(2), 2, 1'000'000, 1'000'000'000, false, false, false, false, 0},
	};

	ldp_slave_sync_config s_cfg_io[] = {
		{SYNC_PORT(0), 32, true},
		{SYNC_PORT(0), 32, true},
	};
	ldp_slave_async_config s_cfg_async_io[] = {
		{ASYNC_PORT(2), 32},
		{ASYNC_PORT(2), 32},
	};

	mpu_conn[0] = m.create(false, &m_cfg_io[0]);
	mpu_conn[1] = m.create(false, &m_cfg_io[1]);
	mpu_conn[2] = m.create(true, &m_cfg_async_io[0]);
	mpu_conn[3] = m.create(true, &m_cfg_async_io[1]);
	s_conn[0] = s1.create(false, &s_cfg_io[0]);
	s_conn[1] = s2.create(false, &s_cfg_io[1]);
	s_conn[2] = s1.create(true, &s_cfg_async_io[0]);
	s_conn[3] = s2.create(true, &s_cfg_async_io[1]);

	ASSERT_EQ(mpu_conn[0], 0);
	ASSERT_EQ(mpu_conn[1], -err::LDP_ERR_INVALID); /* bandwidth control denied */
	ASSERT_EQ(mpu_conn[2], 1);
	ASSERT_EQ(mpu_conn[3], 2); /* pps is not enough, but async port doesn't have our promise */

	m.send(mpu_conn[0], (const uint8_t *)"io:100", 8);
	m.send(mpu_conn[2], (const uint8_t *)"aio:200", 8);
	m.send(mpu_conn[3], (const uint8_t *)"aio:300", 8);

	s1.send(s_conn[0], (const uint8_t *)"io>100\0", 8);
	s1.send(s_conn[2], (const uint8_t *)"aio>200\0", 8);
	s2.send(s_conn[3], (const uint8_t *)"aio>300\0", 8);

	uint8_t rx_buf[32];
	{
		/* Due to the required bandwidth, the second async port won't be processed for a while */
		wq.sync();
		wq.sync();

		ASSERT_EQ(m.recv(mpu_conn[0], rx_buf, 32), 8);
		ASSERT_STREQ((const char *)rx_buf, "io>100");
		ASSERT_EQ(m.recv(mpu_conn[2], rx_buf, 32), 8);
		ASSERT_STREQ((const char *)rx_buf, "aio>200");
		ASSERT_EQ(m.recv(mpu_conn[3], rx_buf, 32), -err::LDP_ERR_AGAIN);
	}

	int cnt = 0;
	while (1) {
		if (m.recv(mpu_conn[3], rx_buf, 32) == 8) {
			break;
		}
		wq.sync();
		cnt++;
	}
	ASSERT_STREQ((const char *)rx_buf, "aio>300");
	printf("%d times\n", cnt);
	ASSERT_TRUE(cnt > 3);
}

TEST_F(test_ldp_sm, async_reset_master_sw)
{
	/* Master reset will not affect the slave.
	 * Due to the async port is using xid to identify the data,
	 * the xid will be reset to 0 after master reset. So we need to
	 * make sure that the xid wouldn't affect the slave in this scenario.
	 */
	using m_t = std::unique_ptr<ldp_master_testable>;
	simu_work_queue wq;
	simu_mcb bus_m(0), bus_s(1);
	m_t m;
	ldp_slave_impl s(&bus_s);

	int conn_m, conn_s;

	ldp_master_async_config m_cfg_cfg[] = {
		{ASYNC_PORT(0), 1, 1000, 10000, false, false, false, false},
	};
	ldp_slave_async_config s_cfg_cfg[] = {
		{ASYNC_PORT(0), 32},
	};

	m = std::make_unique<ldp_master_testable>(&bus_m, &wq);
	conn_m = m->create(true, &m_cfg_cfg[0]);
	conn_s = s.create(true, &s_cfg_cfg[0]);
	ASSERT_EQ(conn_m, 0);
	ASSERT_EQ(conn_s, 0);

	uint8_t rx_buf[32];

	/* First round, master send config data and slave response */
	m->send(conn_m, (const uint8_t *)"aio:000", 8);
	m->schedule_bc(1000);
	wq.sync();
	ASSERT_EQ(s.recv(conn_s, rx_buf, 32), 8);
	ASSERT_EQ(s.send(conn_s, (const uint8_t *)"aio>000", 8), 8);

	/* Second round, master recv config data */
	m->schedule_bc(1000);
	wq.sync();
	ASSERT_EQ(m->recv(conn_m, rx_buf, 32), 8);
	ASSERT_STREQ((const char *)rx_buf, "aio>000");

	/* After master reset, the step 1&2 will be reproduced, and it should be ok */
	m = nullptr;

	m = std::make_unique<ldp_master_testable>(&bus_m, &wq);
	conn_m = m->create(true, &m_cfg_cfg[0]);
	ASSERT_EQ(conn_m, 0);

	/* 3nd, master detect the conflict resp, then retries until slave receives the new xid */
	m->send(conn_m, (const uint8_t *)"aio:001", 8);
	m->schedule_bc(1000);
	wq.sync();
	ASSERT_EQ(m->recv(conn_m, rx_buf, 32), -err::LDP_ERR_AGAIN);
	pump_until_slave_payload(wq, *m, s, conn_s, "aio:001", 8);

	/* One more poll lets the software slave observe the ack for its stale response. */
	m->schedule_bc(1000);
	wq.sync();
	ASSERT_EQ(m->recv(conn_m, rx_buf, 32), -err::LDP_ERR_AGAIN);
	ASSERT_EQ(s.send(conn_s, (const uint8_t *)"aio>001", 8), 8);

	/* 4nd, master recv the right feed back */
	pump_until_master_payload(wq, *m, conn_m, "aio>001", 8);
}

TEST_F(test_ldp_sm, async_reset_master_sw_failed)
{
	/* Master reset will not affect the slave.
	 * Due to the async port is using xid to identify the data,
	 * the xid will be reset to 0 after master reset. So we need to
	 * make sure that the xid wouldn't affect the slave in this scenario.
	 * But if the master assumes the slave is hardware implemented, and
	 * the actual slave is implemented in software, the slave transaction
	 * would be blocked.
	 */
	using m_t = std::unique_ptr<ldp_master_testable>;
	simu_work_queue wq;
	simu_mcb bus_m(0), bus_s(1);
	m_t m;
	ldp_slave_impl s(&bus_s);

	int conn_m, conn_s;

	ldp_master_async_config m_cfg_cfg[] = {
		{ASYNC_PORT(0), 1, 1000, 10000, false, false, false, true},
	};
	ldp_slave_async_config s_cfg_cfg[] = {
		{ASYNC_PORT(0), 32},
	};

	m = std::make_unique<ldp_master_testable>(&bus_m, &wq);
	conn_m = m->create(true, &m_cfg_cfg[0]);
	conn_s = s.create(true, &s_cfg_cfg[0]);
	ASSERT_EQ(conn_m, 0);
	ASSERT_EQ(conn_s, 0);

	uint8_t rx_buf[32];

	/* First round, master send config data and slave response */
	m->send(conn_m, (const uint8_t *)"aio:000", 8);
	m->schedule_bc(1000);
	wq.sync();
	ASSERT_EQ(m->recv(conn_m, rx_buf, 32), -err::LDP_ERR_AGAIN);
	ASSERT_EQ(s.recv(conn_s, rx_buf, 32), 8);
	ASSERT_EQ(s.send(conn_s, (const uint8_t *)"aio>000", 8), 8);

	/* Second round, master recv config data */
	m->schedule_bc(1000);
	wq.sync();
	ASSERT_EQ(m->recv(conn_m, rx_buf, 32), 8);
	ASSERT_STREQ((const char *)rx_buf, "aio>000");
	ASSERT_EQ(s.recv(conn_s, rx_buf, 32), -err::LDP_ERR_AGAIN);

	/* After master reset, the step 1&2 will be reproduced, and it should be not ok*/
	m = nullptr;

	m = std::make_unique<ldp_master_testable>(&bus_m, &wq);
	conn_m = m->create(true, &m_cfg_cfg[0]);
	ASSERT_EQ(conn_m, 0);

	/* 3nd, master will recv the conflict resp, and slave won't recv anything */
	m->send(conn_m, (const uint8_t *)"aio:001", 8);
	m->schedule_bc(1000);
	wq.sync();
	ASSERT_EQ(m->recv(conn_m, rx_buf, 32), 8);
	ASSERT_STREQ((const char *)rx_buf, "aio>000");
	ASSERT_EQ(s.recv(conn_s, rx_buf, 32), -err::LDP_ERR_AGAIN);

	/* 4nd, slave won't recv anything */
	m->schedule_bc(1000);
	wq.sync();
	ASSERT_EQ(m->recv(conn_m, rx_buf, 32), -err::LDP_ERR_AGAIN);
	ASSERT_EQ(s.recv(conn_s, rx_buf, 32), -err::LDP_ERR_AGAIN);
}

TEST_F(test_ldp_sm, statistics_sync)
{
	simu_work_queue wq;
	simu_mcb bus_m(0), bus_s(1);
	ldp_master_testable m(&bus_m, &wq);
	ldp_slave_impl s(&bus_s);
	port_stat m_stat;

	int conn_m, conn_s;

	/* Create a sync port for master and slave, check status before/after transmit */
	ldp_master_sync_config m_cfg_io[] = {
		{SYNC_PORT(0), 1, 1'000'000, 1'000'000, false, false},
	};
	ldp_slave_sync_config s_cfg_io[] = {
		{SYNC_PORT(0), 32, true},
	};

	conn_m = m.create(false, &m_cfg_io[0]);
	conn_s = s.create(false, &s_cfg_io[0]);
	ASSERT_EQ(conn_m, 0);
	ASSERT_EQ(conn_s, 0);

	ASSERT_TRUE(m.get_statistic(conn_m, &m_stat));
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_TX_COUNT), 0);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_RX_COUNT), 0);

	m.send(conn_m, (const uint8_t *)"io:000", 8);
	s.send(conn_s, (const uint8_t *)"io>000\0", 8);

	m.get_statistic(conn_m, &m_stat);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_TX_COUNT), 1);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_TX_BYTES), 8);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_HIST_XFER_COUNT), 0);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_RX_COUNT), 0);

	wq.sync();
	m.get_statistic(conn_m, &m_stat);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_TX_COUNT), 0);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_TX_BYTES), 0);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_RX_COUNT), 1);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_RX_BYTES), 8);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_HIST_XFER_COUNT), 1);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_RX_COUNT), 1);
}

TEST_F(test_ldp_sm, statistics_async)
{
	simu_work_queue wq;
	simu_mcb bus_m(0), bus_s(1);
	ldp_master_testable m(&bus_m, &wq);
	ldp_slave_impl s(&bus_s);
	port_stat m_stat;

	int conn_m, conn_s;

	/* Create a async port for master and slave, check status before/after transmit */
	ldp_master_async_config m_cfg_cfg[] = {
		{ASYNC_PORT(0), 1, 1'000'000, 1'000'000, false, false, false},
	};
	ldp_slave_async_config s_cfg_cfg[] = {
		{ASYNC_PORT(0), 32},
	};

	conn_m = m.create(true, &m_cfg_cfg[0]);
	conn_s = s.create(true, &s_cfg_cfg[0]);
	ASSERT_EQ(conn_m, 0);
	ASSERT_EQ(conn_s, 0);

	ASSERT_TRUE(m.get_statistic(conn_m, &m_stat));
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_TX_COUNT), 0);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_RX_COUNT), 0);

	m.send(conn_m, (const uint8_t *)"aio:000", 8);
	s.send(conn_s, (const uint8_t *)"aio>000\0", 8);

	m.get_statistic(conn_m, &m_stat);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_TX_COUNT), 1);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_TX_BYTES), 8);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_HIST_XFER_COUNT), 0);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_RX_COUNT), 0);

	/* For the first round, the master will recv the data from slave, rx complete */
	m.schedule_bc(1000);
	wq.sync();
	m.get_statistic(conn_m, &m_stat);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_TX_COUNT), 1);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_TX_BYTES), 8);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_RX_COUNT), 1);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_RX_BYTES), 8);
	/* learner keeps the next async retry explicit, so one wq.sync() means one xfer */
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_HIST_XFER_COUNT), 1);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_HIST_RX_COUNT), 1);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_HIST_RX_COUNT_WITH_ACK), 0);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_HIST_RX_COUNT_WITH_DATA), 1);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_RX_COUNT), 1);

	uint8_t buf[32];
	ASSERT_EQ(m.recv(conn_m, buf, 32), 8);
	ASSERT_STREQ((const char *)buf, "aio>000");
	ASSERT_EQ(m.recv(conn_s, buf, 32), -err::LDP_ERR_AGAIN);
	ASSERT_EQ(s.recv(conn_s, buf, 32), 8);
	ASSERT_STREQ((const char *)buf, "aio:000");
	/* Sync stat again, the blocking recv shall be cleared */
	m.get_statistic(conn_m, &m_stat);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_RX_COUNT), 0);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_RX_BYTES), 0);

	/* For the second round, the master will recv the ack pack, tx complete */
	m.schedule_bc(1000);
	wq.sync();
	m.get_statistic(conn_m, &m_stat);
	EXPECT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_TX_COUNT), 0);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_TX_BYTES), 0);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_HIST_XFER_COUNT), 2);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_RX_COUNT), 0);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_BLOCKING_RX_BYTES), 0);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_HIST_RX_COUNT), 2);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_HIST_RX_COUNT_WITH_ACK), 1);
	ASSERT_EQ(m_stat.val(port_stat::STAT_ID_HIST_RX_COUNT_WITH_DATA), 1);
}


TEST_F(test_ldp_sm, async_delay_steady_state_uses_p50_not_p90)
{
	simu_work_queue wq;
	simu_mcb bus_m(0), bus_s(1);
	ldp_master_testable m(&bus_m, &wq);
	ldp_slave_impl s(&bus_s);

	ldp_master_async_config m_cfg = {ASYNC_PORT(0), 1, 5000, 50000, false, false, false, false, 0};
	ldp_slave_async_config s_cfg = {ASYNC_PORT(0), 32};
	int conn_m = m.create(true, &m_cfg);
	int conn_s = s.create(true, &s_cfg);
	ASSERT_EQ(conn_m, 0);
	ASSERT_EQ(conn_s, 0);

	const char *payloads[] = {"rsp:100", "rsp:101", "rsp:102", "rsp:103", "rsp:104"};
	const uint32_t delays[] = {1000, 1000, 1000, 1000, 4000};

	for (size_t i = 0; i < std::size(delays); ++i) {
		m.schedule_bc(1000);
		queue_slave_payload(s, conn_s, bus_s.sid_, ASYNC_PORT(0), payloads[i], delays[i]);
		run_async_round(wq, m);
		expect_master_payload(m, conn_m, payloads[i]);
	}

	auto &item = require_async_item(wq, m);
	EXPECT_LT(item.last_reset_delay, 2000u);
	EXPECT_LT(item.last_reset_delay, item.last_reset_cycle);
}

TEST_F(test_ldp_sm, async_delay_first_short_miss_steps_to_guard_then_cycle)
{
	simu_work_queue wq;
	simu_mcb bus_m(0), bus_s(1);
	ldp_master_testable m(&bus_m, &wq);
	ldp_slave_impl s(&bus_s);

	ldp_master_async_config m_cfg = {ASYNC_PORT(0), 1, 5000, 50000, false, false, false, false, 0};
	ldp_slave_async_config s_cfg = {ASYNC_PORT(0), 32};
	int conn_m = m.create(true, &m_cfg);
	int conn_s = s.create(true, &s_cfg);
	ASSERT_EQ(conn_m, 0);
	ASSERT_EQ(conn_s, 0);

	const char *payloads[] = {"rsp:200", "rsp:201", "rsp:202", "rsp:203", "rsp:204"};
	const uint32_t delays[] = {1000, 1000, 1000, 1000, 4000};

	for (size_t i = 0; i < std::size(delays); ++i) {
		m.schedule_bc(1000);
		queue_slave_payload(s, conn_s, bus_s.sid_, ASYNC_PORT(0), payloads[i], delays[i]);
		run_async_round(wq, m);
		expect_master_payload(m, conn_m, payloads[i]);
	}

	auto &item = require_async_item(wq, m);
	uint32_t base_delay = item.last_reset_delay;
	ASSERT_LT(base_delay, 2000u);

	m.schedule_bc(1000);
	run_async_round(wq, m);
	uint32_t guard_delay = item.last_reset_delay;
	EXPECT_GT(guard_delay, base_delay);
	EXPECT_LT(guard_delay, item.last_reset_cycle);

	m.schedule_bc(1000);
	run_async_round(wq, m);
	EXPECT_EQ(item.last_reset_delay, item.last_reset_cycle);
}

TEST_F(test_ldp_sm, async_delay_abrupt_rise_uses_latest_sample_for_guard)
{
	simu_work_queue wq;
	simu_mcb bus_m(0), bus_s(1);
	ldp_master_testable m(&bus_m, &wq);
	ldp_slave_impl s(&bus_s);

	ldp_master_async_config m_cfg = {ASYNC_PORT(0), 1, 5000, 50000, false, false, false, false, 0};
	ldp_slave_async_config s_cfg = {ASYNC_PORT(0), 32};
	int conn_m = m.create(true, &m_cfg);
	int conn_s = s.create(true, &s_cfg);
	ASSERT_EQ(conn_m, 0);
	ASSERT_EQ(conn_s, 0);

	for (int i = 0; i < 15; ++i) {
		char payload[8] = { 'r', 's', 'p', ':', '3', '0', static_cast<char>('0' + (i % 10)), '\0' };
		m.schedule_bc(1000);
		queue_slave_payload(s, conn_s, bus_s.sid_, ASYNC_PORT(0), payload, 1000);
		run_async_round(wq, m);
		expect_master_payload(m, conn_m, payload);
	}

	m.schedule_bc(1000);
	queue_slave_payload(s, conn_s, bus_s.sid_, ASYNC_PORT(0), "rsp:3S0", 3000);
	run_async_round(wq, m);
	expect_master_payload(m, conn_m, "rsp:3S0");

	auto &item = require_async_item(wq, m);
	uint32_t base_delay = item.last_reset_delay;
	EXPECT_LT(base_delay, 2000u);

	m.schedule_bc(1000);
	run_async_round(wq, m);
	EXPECT_GT(item.last_reset_delay, 3000u);
	EXPECT_LT(item.last_reset_delay, item.last_reset_cycle);
}

TEST_F(test_ldp_sm, async_delay_full_cycle_ineffective_streak_clears_history)
{
	simu_work_queue wq;
	simu_mcb bus_m(0), bus_s(1);
	ldp_master_testable m(&bus_m, &wq);
	ldp_slave_impl s(&bus_s);

	ldp_master_async_config m_cfg = {ASYNC_PORT(0), 1, 5000, 50000, false, false, false, false, 0};
	ldp_slave_async_config s_cfg = {ASYNC_PORT(0), 32};
	int conn_m = m.create(true, &m_cfg);
	int conn_s = s.create(true, &s_cfg);
	ASSERT_EQ(conn_m, 0);
	ASSERT_EQ(conn_s, 0);

	for (int i = 0; i < 4; ++i) {
		char payload[8] = { 'r', 's', 'p', ':', '4', '0', static_cast<char>('0' + i), '\0' };
		m.schedule_bc(1000);
		queue_slave_payload(s, conn_s, bus_s.sid_, ASYNC_PORT(0), payload, 3000);
		run_async_round(wq, m);
		expect_master_payload(m, conn_m, payload);
	}

	auto &item = require_async_item(wq, m);
	ASSERT_GT(item.last_reset_delay, 3000u);

	m.schedule_bc(1000);
	run_async_round(wq, m);
	m.schedule_bc(1000);
	run_async_round(wq, m);
	EXPECT_EQ(item.last_reset_delay, item.last_reset_cycle);

	for (int i = 0; i < 3; ++i) {
		m.schedule_bc(1000);
		run_async_round(wq, m);
		EXPECT_EQ(item.last_reset_delay, item.last_reset_cycle);
	}

	m.schedule_bc(1000);
	queue_slave_payload(s, conn_s, bus_s.sid_, ASYNC_PORT(0), "rsp:4R0", 1000);
	run_async_round(wq, m);
	expect_master_payload(m, conn_m, "rsp:4R0");
	EXPECT_LT(item.last_reset_delay, 2000u);
}

TEST_F(test_ldp_sm, async_delay_bc_denial_schedules_full_cycle)
{
	simu_work_queue wq;
	simu_mcb bus_m(0, simu_mcb::ps_io, 1), bus_s(1);
	ldp_master_testable m(&bus_m, &wq);
	ldp_slave_impl s(&bus_s);

	ldp_master_async_config m_cfg = {ASYNC_PORT(0), 1, 5000, 50000, false, false, false, false, 1};
	ldp_slave_async_config s_cfg = {ASYNC_PORT(0), 32};
	int conn_m = m.create(true, &m_cfg);
	int conn_s = s.create(true, &s_cfg);
	ASSERT_EQ(conn_m, 0);
	ASSERT_EQ(conn_s, 0);

	m.schedule_bc(1500000);
	queue_slave_payload(s, conn_s, bus_s.sid_, ASYNC_PORT(0), "rsp:500", 1000);
	run_async_round(wq, m);
	expect_master_payload(m, conn_m, "rsp:500");

	auto &item = require_async_item(wq, m);
	EXPECT_EQ(item.last_reset_delay, item.last_reset_cycle);
}

TEST_F(test_ldp_sm, slave_ready_mask_filters_unopened_ports)
{
	simu_mcb bus_s(1);
	ldp_slave_impl s(&bus_s);
	ldp_slave_sync_config cfg = {0x03, 32, false};
	int conn = s.create(false, &cfg);

	ASSERT_EQ(conn, 0);

	simu_mcb::data_ready_[bus_s.sid_][0x03] = true;
	simu_mcb::data_ready_[bus_s.sid_][0x04] = true;

	EXPECT_EQ(s.get_rx_port_mask(), (1u << 0x03));

	ASSERT_EQ(s.destroy(conn), 0);
	EXPECT_EQ(s.get_rx_port_mask(), 0u);
}

TEST_F(test_ldp_sm, master_destroy_keeps_ready_mask_while_same_port_still_open)
{
	simu_work_queue wq;
	simu_mcb bus_m(0);
	ldp_master_testable m(&bus_m, &wq);
	ldp_master_async_config cfg_a = {ASYNC_PORT(0), 1, 5000, 50000, false, false, false, false,
					 0};
	ldp_master_async_config cfg_b = {ASYNC_PORT(0), 2, 5000, 50000, false, false, false, false,
					 0};
	int conn_a = m.create(true, &cfg_a);
	int conn_b = m.create(true, &cfg_b);

	ASSERT_EQ(conn_a, 0);
	ASSERT_EQ(conn_b, 1);

	simu_mcb::data_ready_[bus_m.sid_][ASYNC_PORT(0)] = true;
	EXPECT_EQ(m.get_rx_port_mask(), (1u << ASYNC_PORT(0)));

	ASSERT_EQ(m.destroy(conn_a), 0);
	EXPECT_EQ(m.get_rx_port_mask(), (1u << ASYNC_PORT(0)));

	ASSERT_EQ(m.destroy(conn_b), 0);
	EXPECT_EQ(m.get_rx_port_mask(), 0u);
}

TEST_F(test_ldp_sm, first_sync_connection_wakes_idle_sync_queue)
{
	simu_work_queue wq;
	simu_mcb bus_m(0);
	ldp_master_testable m(&bus_m, &wq);
	auto *item = wq.find_item(m.sync_wq_id());
	ldp_master_sync_config cfg = {0x02, 1, 10000, 50000, false, false};
	int conn;

	ASSERT_NE(item, nullptr);
	wq.sync(item->time_left > 0 ? static_cast<uint32_t>(item->time_left) : 0u);

	ASSERT_EQ(item->last_reset_delay, 100000u);
	ASSERT_EQ(item->last_reset_cycle, 10000u);

	conn = m.create(false, &cfg);
	ASSERT_EQ(conn, 0);
	EXPECT_LE(item->last_reset_delay, item->last_reset_cycle);
	EXPECT_NE(item->last_reset_delay, 100000u);
}

TEST_F(test_ldp_sm, first_async_connection_wakes_bandwidth_control_queue)
{
	simu_work_queue wq;
	simu_mcb bus_m(0);
	ldp_master_testable m(&bus_m, &wq);
	auto *item = wq.find_item(m.bc_wq_id());
	ldp_master_async_config cfg = {ASYNC_PORT(0), 1, 5000, 50000, false, false, false, false,
				       0};
	int conn;

	ASSERT_NE(item, nullptr);
	ASSERT_EQ(item->last_reset_cycle, 1000000u);
	ASSERT_EQ(item->last_reset_delay, 1000000u);

	conn = m.create(true, &cfg);
	ASSERT_EQ(conn, 0);
	EXPECT_LT(item->last_reset_delay, item->last_reset_cycle);
}

TEST_F(test_ldp_sm, second_sync_connection_does_not_wake_active_sync_queue)
{
	simu_work_queue wq;
	simu_mcb bus_m(0);
	ldp_master_testable m(&bus_m, &wq);
	auto *item = wq.find_item(m.sync_wq_id());
	ldp_master_sync_config cfg_a = {0x02, 1, 10000, 50000, false, false};
	ldp_master_sync_config cfg_b = {0x03, 1, 10000, 50000, false, false};
	int conn_a;
	int conn_b;

	ASSERT_NE(item, nullptr);

	conn_a = m.create(false, &cfg_a);
	ASSERT_EQ(conn_a, 0);
	ASSERT_EQ(item->reset_count, 1u);

	conn_b = m.create(false, &cfg_b);
	ASSERT_EQ(conn_b, 1);
	EXPECT_EQ(item->reset_count, 1u);
}

TEST_F(test_ldp_sm, second_async_connection_does_not_wake_active_bandwidth_control_queue)
{
	simu_work_queue wq;
	simu_mcb bus_m(0);
	ldp_master_testable m(&bus_m, &wq);
	auto *item = wq.find_item(m.bc_wq_id());
	ldp_master_async_config cfg_a = {ASYNC_PORT(0), 1, 5000, 50000, false, false, false, false,
				       0};
	ldp_master_async_config cfg_b = {ASYNC_PORT(1), 1, 5000, 50000, false, false, false, false,
				       0};
	int conn_a;
	int conn_b;

	ASSERT_NE(item, nullptr);

	conn_a = m.create(true, &cfg_a);
	ASSERT_EQ(conn_a, 0);
	ASSERT_EQ(item->reset_count, 1u);

	conn_b = m.create(true, &cfg_b);
	ASSERT_EQ(conn_b, 1);
	EXPECT_EQ(item->reset_count, 1u);
}
