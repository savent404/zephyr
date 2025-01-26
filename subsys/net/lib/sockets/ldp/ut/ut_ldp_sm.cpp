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

using ldp_master_impl = ldp_master<mock_mempool, dummy_cache>;
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
std::list<void *> mock_mempool::ptrs;

using err = ldp_basic::ldp_error;

TEST_F(test_ldp_sm, basic_concept)
{
	simu_work_queue wq;
	simu_mcb bus_m(0), bus_s1(1), bus_s2(2);
	ldp_master_impl m(&bus_m, &wq);
	ldp_slave_impl s1(&bus_s1), s2(&bus_s2);

	ldp_master_async_config m_cfg_cfg[2] = {
		{0x10, 1, 1000, 2000, false, true, true},
		{0x10, 2, 1000, 2000, false, true, true},
	};
	ldp_master_sync_config m_cfg_io[2] = {
		{0x40, 1, 1000, false, false},
		{0x40, 2, 1000, false, false},
	};
	ldp_master_async_config m_cfg_async_io[2] = {
		{0x60, 1, 1000, 2000, false, false, false},
		{0x60, 2, 1000, 2000, false, false, false},
	};

	ldp_slave_async_config s_cfg_cfg[2] = {
		{0x10, 32},
		{0x10, 8},
	};
	ldp_slave_sync_config s_cfg_io[2] = {
		{0x40, 32, true},
		{0x40, 8, true},
	};
	ldp_slave_async_config s_cfg_async_io[2] = {
		{0x60, 32},
		{0x60, 8},
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

TEST_F(test_ldp_sm, DISABLED_harq)
{
	simu_work_queue wq;
	simu_mcb bus_m(0), bus_s1(1), bus_s2(2);
	ldp_master_impl m(&bus_m, &wq);
	ldp_slave_impl s1(&bus_s1), s2(&bus_s2);

	ldp_master_async_config m_cfg_cfg[] = {
		{0x60, 1, 1000, 10000, false, false, false},
	};
	ldp_slave_async_config s_cfg_cfg[] = {
		{0x60, 32},
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

TEST_F(test_ldp_sm, async_timeout)
{
	simu_work_queue wq;
	simu_mcb bus_m(0), bus_s1(1);
	ldp_master_impl m(&bus_m, &wq);
	ldp_slave_impl s1(&bus_s1);

	ldp_master_async_config m_cfg_cfg[] = {
		{0x10, 1, 1000, 3000, false, true, true},
		{0x60, 1, 1000, 3000, false, false, false},
	};
	ldp_slave_async_config s_cfg_cfg[] = {
		{0x10, 32},
		{0x60, 32},
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

	for (int i = 0; i < 3; i++) {
		wq.sync();
		ASSERT_EQ(m.recv(m_conn[0], rx_buf, 32), -err::LDP_ERR_AGAIN);
		ASSERT_EQ(m.recv(m_conn[1], rx_buf, 32), -err::LDP_ERR_AGAIN);
	}
	wq.sync();
	ASSERT_EQ(m.recv(m_conn[0], rx_buf, 32), -err::LDP_ERR_ATIMEOUT);
	ASSERT_EQ(m.recv(m_conn[1], rx_buf, 32), -err::LDP_ERR_ATIMEOUT);

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
	ldp_master_impl m(&bus_m, &wq);
	ldp_slave_impl s1(&bus_s1), s2(&bus_s2);

	ldp_master_async_config m_cfg_cfg[2] = {
		{0x10, 1, 1000, 10000, false, true, true},
		{0x10, 2, 1000, 10000, false, true, true},
	};
	ldp_master_sync_config m_cfg_io[2] = {
		{0x40, 1, 1000, false, false},
		{0x40, 2, 1000, false, false},
	};
	ldp_master_async_config m_cfg_async_io[2] = {
		{0x60, 1, 1000, 2000, false, false, false},
		{0x60, 2, 1000, 2000, false, false, false},
	};

	ldp_slave_async_config s_cfg_cfg[2] = {
		{0x10, 32},
		{0x10, 8},
	};
	ldp_slave_sync_config s_cfg_io[2] = {
		{0x40, 32, true},
		{0x40, 8, true},
	};
	ldp_slave_async_config s_cfg_async_io[2] = {
		{0x60, 32},
		{0x60, 8},
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
		wq.sync();
		/* NOTE: master will drop this response since strong order is enabled */
		ASSERT_EQ(m.recv(m_conn_cfg[0], rx_buf, 32), -err::LDP_ERR_AGAIN);
		ASSERT_EQ(m.recv(m_conn_cfg[1], rx_buf, 32), -err::LDP_ERR_P_ERROR);
		ASSERT_EQ(m.recv(m_conn_io[0], rx_buf, 32), 8);
		ASSERT_EQ(m.recv(m_conn_io[1], rx_buf, 32), -err::LDP_ERR_P_ERROR);
		ASSERT_EQ(m.recv(m_conn_async_io[0], rx_buf, 32), -err::LDP_ERR_AGAIN);
		ASSERT_EQ(m.recv(m_conn_async_io[1], rx_buf, 32), 8);
	}

	{
		/* Master will drop the response if the response is not updated by slave */
		m.send(m_conn_cfg[0], (const uint8_t *)"cfg:000", 8);
		wq.sync();
		ASSERT_EQ(m.recv(m_conn_cfg[0], rx_buf, 32), -err::LDP_ERR_AGAIN);

		wq.sync();
		ASSERT_EQ(m.recv(m_conn_cfg[0], rx_buf, 32), -err::LDP_ERR_AGAIN);

		s1.recv(s_conn_cfg[0], rx_buf, 32);
		ASSERT_EQ(s1.send(s_conn_cfg[0], (const uint8_t *)"cfg>000", 8), 8);
		wq.sync();
		ASSERT_EQ(m.recv(m_conn_cfg[0], rx_buf, 32), 8);
		wq.sync();
	}
}

TEST_F(test_ldp_sm, master_slave_switch)
{
	simu_work_queue wq;
	simu_mcb bus_mpu(0), bus_mpu_bak(1), bus_io(2);

	auto mpu = std::make_unique<ldp_master_impl>(&bus_mpu, &wq);
	std::unique_ptr<ldp_slave_impl> mpu_s = nullptr;
	std::unique_ptr<ldp_master_impl> s1_m = nullptr;
	auto s1 = std::make_unique<ldp_slave_impl>(&bus_mpu_bak);
	auto s2 = std::make_unique<ldp_slave_impl>(&bus_io);

	int mpu_conn[6], mpu_bak_conn[6], io_conn[3];

	/* case 1: MPU switch to master, mpu_bak switch to slave */
	{
		const ldp_master_async_config m_cfg_cfg[] = {
			{0x10, 1, 1000, 10000, false, true, true},
			{0x60, 1, 1000, 10000, false, false, false},
			{0x10, 2, 1000, 10000, false, true, true},
			{0x60, 2, 1000, 10000, false, false, false},
		};
		const ldp_master_sync_config m_cfg_io[] = {
			{0x40, 1, 1000, false, false},
			{0x40, 2, 1000, false, false},
		};
		mpu_conn[0] = mpu->create(true, &m_cfg_cfg[0]);
		mpu_conn[1] = mpu->create(true, &m_cfg_cfg[1]);
		mpu_conn[2] = mpu->create(true, &m_cfg_cfg[2]);
		mpu_conn[3] = mpu->create(true, &m_cfg_cfg[3]);
		mpu_conn[4] = mpu->create(false, &m_cfg_io[0]);
		mpu_conn[5] = mpu->create(false, &m_cfg_io[1]);

		ldp_slave_async_config s_cfg_cfg[] = {
			{0x10, 32},
			{0x60, 32},
		};
		ldp_slave_sync_config s_cfg_io[] = {
			{0x40, 32, true},
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

		s1_m = std::make_unique<ldp_master_impl>(&bus_mpu_bak, &wq);
		ldp_master_sync_config cfg_io[] = {
			{0x40, 0, 1000, true, false},
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
		auto extra_err = mpu->get_extra_error(mpu_conn[4]);
		mpu->clr_extra_error(mpu_conn[4], extra_err);
		ASSERT_EQ(mpu->get_extra_error(mpu_conn[4]), 0);
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
			{0x10, 32},
			{0x60, 32},
		};
		ldp_slave_sync_config s_cfg_io[] = {
			{0x40, 32, true},
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
			{0x40, 0, 1000, true, false},
		};
		mpu_bak_conn[0] = s1_m->create(false, &cfg_io[0]);
	}
}

TEST_F(test_ldp_sm, async_recv_memleak)
{
	simu_work_queue wq;
	simu_mcb bus_mpu(0), bus_mpu_bak(1), bus_io(2);

	auto mpu = std::make_unique<ldp_master_impl>(&bus_mpu, &wq);
	auto s1 = std::make_unique<ldp_slave_impl>(&bus_mpu_bak);
	auto s2 = std::make_unique<ldp_slave_impl>(&bus_io);

	int mpu_conn[2];
	int s_conn[2];

	ldp_master_async_config m_cfg_cfg[] = {
		{0x60, 1, 1000, 10000, false, false, false},
		{0x60, 2, 1000, 10000, false, false, false},
	};
	ldp_slave_async_config s_cfg_cfg[] = {
		{0x60, 32},
		{0x60, 32},
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
	s1->send(s_conn[0], (const uint8_t *)"res>00C\0", 12);
	s2->send(s_conn[1], (const uint8_t *)"res>010\0", 16);
	wq.sync();
	ASSERT_EQ(mpu->recv(mpu_conn[0], rx_buf, 32), 12);
	ASSERT_STREQ((const char *)rx_buf, "res>00C");
	ASSERT_EQ(mpu->recv(mpu_conn[1], rx_buf, 32), 16);
	ASSERT_STREQ((const char *)rx_buf, "res>010");
	ASSERT_EQ(s1->recv(s_conn[0], rx_buf, 32), 8);
	ASSERT_STREQ((const char *)rx_buf, "aio:002");
	ASSERT_EQ(s2->recv(s_conn[1], rx_buf, 32), 8);
	ASSERT_STREQ((const char *)rx_buf, "aio:003");
}

TEST_F(test_ldp_sm, sync_send_memleak)
{
	simu_work_queue wq;
	simu_mcb bus_mpu(0), bus_mpu_bak(1), bus_io(2);

	auto mpu = std::make_unique<ldp_master_impl>(&bus_mpu, &wq);
	auto s1 = std::make_unique<ldp_slave_impl>(&bus_mpu_bak);
	auto s2 = std::make_unique<ldp_slave_impl>(&bus_io);

	int mpu_conn[2];
	int s_conn[2];

	ldp_master_sync_config m_cfg_io[] = {
		{0x40, 1, 1000, true, false},
		{0x40, 2, 1000, true, false},
	};

	ldp_slave_sync_config s_cfg_io[] = {
		{0x40, 32, true},
		{0x40, 32, true},
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
