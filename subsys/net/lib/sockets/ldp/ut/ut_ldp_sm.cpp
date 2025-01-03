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

TEST_F(test_ldp_sm, basic_sync) {

  simu_work_queue wq;

  simu_mcb bus_m(0), bus_s(1);

  ldp_master_impl m(&bus_m, &wq);
  ldp_slave_impl s(&bus_s);

  /* sync dead slave */
  ldp_slave_sync_config s_cfg;
  ldp_master_sync_config m_cfg;
  m_cfg.port = 0x40;
  m_cfg.dst = 0x5;
  m_cfg.cycle_time = 100;
  m_cfg.preempt = false;
  m_cfg.one_shot = false;

  auto dead_conn = m.create(false, &m_cfg);

  m_cfg.port = 0x40;
  m_cfg.dst = 0x1;
  m_cfg.cycle_time = 100;
  auto conn = m.create(false, &m_cfg);

  uint8_t tx_buf[8] = "Hello, ", rx_buf[8] = "World!";
  EXPECT_EQ(m.send(dead_conn, tx_buf, sizeof(tx_buf)), sizeof(tx_buf));
  EXPECT_EQ(m.send(conn, tx_buf, sizeof(tx_buf)), sizeof(tx_buf));

  /* First request, dead_conn shall be timeout, and conn shall be rejected */
  EXPECT_EQ(m.recv(dead_conn, nullptr, 0), -err::LDP_ERR_AGAIN);
  EXPECT_EQ(m.recv(conn, nullptr, 0), -err::LDP_ERR_AGAIN);
  wq.sync();
  EXPECT_EQ(m.recv(dead_conn, nullptr, 0), -err::LDP_ERR_T_ERROR);
  EXPECT_EQ(m.recv(conn, rx_buf, sizeof(rx_buf)), -err::LDP_ERR_P_ERROR);

  /* Second request, slave configured its port, dead conn shall be timeout, and
   * conn shall be accepted with empty response */
  s_cfg.port = 0x40;
  s_cfg.allow_write = true;
  s_cfg.max_recv_len = 8;
  auto s_conn = s.create(false, &s_cfg);

  wq.sync();
  EXPECT_EQ(m.recv(dead_conn, rx_buf, sizeof(rx_buf)), -err::LDP_ERR_T_ERROR);
  EXPECT_EQ(m.recv(conn, rx_buf, sizeof(rx_buf)), -err::LDP_ERR_AGAIN);
  EXPECT_EQ(s.recv(s_conn, rx_buf, sizeof(rx_buf)), sizeof(rx_buf));
  EXPECT_STREQ((const char *)rx_buf, "Hello, ");

  /* Third request, slave setup its tx buffer, conn shall be accepted with real
   * response */
  EXPECT_EQ(s.send(s_conn, tx_buf, sizeof(tx_buf)), sizeof(tx_buf));
  wq.sync();
  EXPECT_EQ(m.recv(dead_conn, rx_buf, sizeof(rx_buf)), -err::LDP_ERR_T_ERROR);
  memset(rx_buf, 0, sizeof(rx_buf));
  EXPECT_EQ(m.recv(conn, rx_buf, sizeof(rx_buf)), sizeof(rx_buf));
  EXPECT_STREQ((const char *)rx_buf, "Hello, ");
  memset(rx_buf, 0, sizeof(rx_buf));
  EXPECT_EQ(s.recv(s_conn, rx_buf, sizeof(rx_buf)), sizeof(rx_buf));
  EXPECT_STREQ((const char *)rx_buf, "Hello, ");
}

TEST_F(test_ldp_sm, basic_async) {

  simu_work_queue wq;
  simu_mcb bus_m(0), bus_s(1);
  ldp_master_impl m(&bus_m, &wq);
  ldp_slave_impl s(&bus_s);

  /* async dead slave */
  ldp_slave_async_config s_cfg;
  ldp_master_async_config m_cfg;

  m_cfg.port = 0x10;
  m_cfg.dst = 0x5;
  m_cfg.cycle_time = 1;
  m_cfg.timeout = 3;
  m_cfg.one_shot = false;
  m_cfg.preempt = false;
  auto dead_conn = m.create(true, &m_cfg);
  EXPECT_EQ(dead_conn, 0);

  m_cfg.dst = 0x1;
  auto conn = m.create(true, &m_cfg);
  EXPECT_EQ(conn, 1);

  uint8_t tx_buf[8] = "Hello, ", rx_buf[8] = "";

  /* First request, dead_conn shall timeout, and conn shall be rejected */
  EXPECT_EQ(m.send(dead_conn, tx_buf, sizeof(tx_buf)), sizeof(tx_buf));
  EXPECT_EQ(m.send(conn, tx_buf, sizeof(tx_buf)), sizeof(tx_buf));
  wq.sync();
  EXPECT_EQ(m.recv(dead_conn, rx_buf, sizeof(rx_buf)), -err::LDP_ERR_T_ERROR);
  EXPECT_EQ(m.recv(conn, rx_buf, sizeof(rx_buf)), -err::LDP_ERR_P_ERROR);

  /* Second request, slave port is open but response empty frame */
  s_cfg.port = 0x10;
  s_cfg.max_recv_len = 8;
  auto s_conn = s.create(true, &s_cfg);

  wq.sync();
  EXPECT_EQ(m.recv(dead_conn, rx_buf, sizeof(rx_buf)), -err::LDP_ERR_T_ERROR);
  EXPECT_EQ(m.recv(conn, rx_buf, sizeof(rx_buf)), -err::LDP_ERR_AGAIN);
  EXPECT_EQ(s.recv(s_conn, rx_buf, sizeof(rx_buf)), sizeof(rx_buf));
  EXPECT_STREQ((const char *)rx_buf, "Hello, ");

  /* Third request, slave port is ready, and async write is really timeout */
  EXPECT_EQ(s.send(s_conn, tx_buf, sizeof(tx_buf)), sizeof(tx_buf));
  wq.sync();
  EXPECT_EQ(m.recv(dead_conn, rx_buf, sizeof(rx_buf)), -err::LDP_ERR_T_ERROR);
  EXPECT_EQ(m.recv(conn, rx_buf, sizeof(rx_buf)), sizeof(rx_buf));
  EXPECT_EQ(s.recv(s_conn, rx_buf, sizeof(rx_buf)), 0);
}

TEST_F(test_ldp_sm, sync_timeout_and_recovery) {
  /* Make sure that get_extra_error() can notice there was a timeout error */
  simu_work_queue wq;

  simu_mcb bus_m(0);
  ldp_master_impl m(&bus_m, &wq);

  ldp_master_sync_config m_cfg;
  m_cfg.port = 0x40;
  m_cfg.dst = 0x1;
  m_cfg.cycle_time = 100;
  m_cfg.one_shot = false;
  m_cfg.preempt = false;
  auto conn = m.create(false, &m_cfg);
  EXPECT_EQ(conn, 0);

  uint8_t tx_buf[8] = "Hello, ", rx_buf[8] = "World!";
  EXPECT_EQ(m.send(conn, tx_buf, sizeof(tx_buf)), sizeof(tx_buf));
  EXPECT_EQ(m.recv(conn, rx_buf, sizeof(rx_buf)), -err::LDP_ERR_AGAIN);
  wq.sync();
  EXPECT_EQ(m.recv(conn, rx_buf, sizeof(rx_buf)), -err::LDP_ERR_T_ERROR);

  simu_mcb bus_s(1);
  ldp_slave_impl s(&bus_s);
  ldp_slave_sync_config s_cfg;
  s_cfg.port = 0x40;
  s_cfg.allow_write = true;
  s_cfg.max_recv_len = 32;
  auto s_conn = s.create(false, &s_cfg);
  EXPECT_EQ(s_conn, 0);
  EXPECT_EQ(s.send(s_conn, tx_buf, sizeof(tx_buf)), sizeof(tx_buf));
  wq.sync();
  EXPECT_EQ(m.recv(conn, rx_buf, sizeof(rx_buf)), sizeof(rx_buf));
  EXPECT_STREQ((const char *)rx_buf, "Hello, ");
  EXPECT_TRUE(m.get_extra_error(conn) & (1 << err::LDP_ERR_PREV_T_ERROR));
}

TEST_F(test_ldp_sm, async_timeout) {
  /* Make sure that async timeout is working */
  simu_work_queue wq;

  simu_mcb bus_m(0), bus_s(1);
  ldp_master_impl m(&bus_m, &wq);
  ldp_slave_impl s(&bus_s);

  ldp_master_async_config m_cfg;
  m_cfg.port = 0x10;
  m_cfg.dst = 0x1;
  m_cfg.cycle_time = 1;
  m_cfg.timeout = 3;
  m_cfg.one_shot = false;
  m_cfg.preempt = false;
  auto conn = m.create(true, &m_cfg);
  EXPECT_EQ(conn, 0);

  ldp_slave_async_config s_cfg;
  s_cfg.port = 0x10;
  s_cfg.max_recv_len = 8;
  auto s_conn = s.create(true, &s_cfg);
  EXPECT_EQ(s_conn, 0);

  uint8_t tx_buf[8] = "Hello, ", rx_buf[8] = "";
  s.send(s_conn, tx_buf, sizeof(tx_buf));
  wq.sync();
  EXPECT_EQ(m.recv(conn, rx_buf, sizeof(rx_buf)), sizeof(rx_buf));

  wq.sync();
  EXPECT_EQ(m.recv(conn, rx_buf, sizeof(rx_buf)), -err::LDP_ERR_AGAIN);

  wq.sync();
  EXPECT_EQ(m.recv(conn, rx_buf, sizeof(rx_buf)), -err::LDP_ERR_AGAIN);

  wq.sync();
  EXPECT_EQ(m.recv(conn, rx_buf, sizeof(rx_buf)), -err::LDP_ERR_AGAIN);

  wq.sync();
  EXPECT_EQ(m.recv(conn, rx_buf, sizeof(rx_buf)), -err::LDP_ERR_ATIMEOUT);
}

TEST_F(test_ldp_sm, one_shot) {
  simu_work_queue wq;

  simu_mcb bus_m(0), bus_s(1, simu_mcb::ps_io);
  ldp_master_impl m(&bus_m, &wq);
  ldp_slave_impl s(&bus_s);

  ldp_master_async_config ma_cfg;
  ldp_slave_async_config sa_cfg;
  ldp_master_sync_config ms_cfg;
  ldp_slave_sync_config ss_cfg;

  ma_cfg.port = 0x10;
  ma_cfg.dst = 0x1;
  ma_cfg.cycle_time = 1;
  ma_cfg.timeout = 1;
  ma_cfg.one_shot = true;
  ma_cfg.preempt = false;
  auto a_conn = m.create(true, &ma_cfg);
  EXPECT_EQ(a_conn, 0);

  ms_cfg.port = 0x40;
  ms_cfg.dst = 0x1;
  ms_cfg.cycle_time = 100;
  ms_cfg.preempt = false;
  ms_cfg.one_shot = true;
  auto s_conn = m.create(false, &ms_cfg);

  sa_cfg.port = 0x10;
  sa_cfg.max_recv_len = 8;
  auto sa_conn = s.create(true, &sa_cfg);

  ss_cfg.port = 0x40;
  ss_cfg.allow_write = true;
  ss_cfg.max_recv_len = 8;
  auto ss_conn = s.create(false, &ss_cfg);

  uint8_t tx_buf[8] = "Foo bar", rx_buf[8] = "", new_tx_buf[8] = "Hello, ";

  /* For one-shot sync, it will be always return buffered data and not up to
   * date */
  /* For one-shot async, it will be always return -EGAIN once it is done */
  EXPECT_EQ(m.send(a_conn, tx_buf, sizeof(tx_buf)), sizeof(tx_buf));
  EXPECT_EQ(m.send(s_conn, tx_buf, sizeof(tx_buf)), sizeof(tx_buf));
  EXPECT_EQ(s.send(sa_conn, tx_buf, sizeof(tx_buf)), sizeof(tx_buf));
  EXPECT_EQ(s.send(ss_conn, tx_buf, sizeof(tx_buf)), sizeof(tx_buf));
  wq.sync();
  EXPECT_EQ(m.recv(a_conn, rx_buf, sizeof(rx_buf)), sizeof(rx_buf));
  EXPECT_STREQ((const char *)rx_buf, "Foo bar");
  EXPECT_EQ(m.recv(s_conn, rx_buf, sizeof(rx_buf)), sizeof(rx_buf));
  EXPECT_STREQ((const char *)rx_buf, "Foo bar");
  EXPECT_EQ(s.recv(sa_conn, rx_buf, sizeof(rx_buf)), sizeof(rx_buf));
  EXPECT_STREQ((const char *)rx_buf, "Foo bar");
  EXPECT_EQ(s.recv(ss_conn, rx_buf, sizeof(rx_buf)), sizeof(rx_buf));
  EXPECT_STREQ((const char *)rx_buf, "Foo bar");

  EXPECT_EQ(m.send(a_conn, new_tx_buf, sizeof(new_tx_buf)), sizeof(new_tx_buf));
  EXPECT_EQ(m.send(s_conn, new_tx_buf, sizeof(new_tx_buf)), sizeof(new_tx_buf));
  EXPECT_EQ(s.send(sa_conn, new_tx_buf, sizeof(new_tx_buf)),
            sizeof(new_tx_buf));
  EXPECT_EQ(s.send(ss_conn, new_tx_buf, sizeof(new_tx_buf)),
            sizeof(new_tx_buf));
  wq.sync();
  EXPECT_EQ(m.recv(a_conn, rx_buf, sizeof(rx_buf)), -err::LDP_ERR_AGAIN);
  EXPECT_EQ(m.recv(s_conn, rx_buf, sizeof(rx_buf)), sizeof(rx_buf));
  EXPECT_STREQ((const char *)rx_buf, "Foo bar"); /* not updated */
  EXPECT_EQ(s.recv(sa_conn, rx_buf, sizeof(rx_buf)), -err::LDP_ERR_AGAIN);
  EXPECT_EQ(s.recv(ss_conn, rx_buf, sizeof(rx_buf)), 0); /* not updated */
}

TEST_F(test_ldp_sm, sync_handle_extra_errors) {
  simu_work_queue wq;

  simu_mcb bus_m(0), bus_s(1);
  ldp_master_impl m(&bus_m, &wq);
  ldp_slave_impl s(&bus_s);

  ldp_master_sync_config ms_cfg;
  ldp_slave_sync_config ss_cfg;

  ms_cfg.port = 0x40;
  ms_cfg.dst = 0x1;
  ms_cfg.cycle_time = 100;
  ms_cfg.preempt = false;
  ms_cfg.one_shot = false;
  auto s_conn = m.create(false, &ms_cfg);

  ss_cfg.port = 0x40;
  ss_cfg.allow_write = true;
  ss_cfg.max_recv_len = 8;
  auto ss_conn = s.create(false, &ss_cfg);

  uint8_t tx_buf[8] = "Hello, ", rx_buf[8] = "";

  /* For async, it will might receive R_ERROR */
  EXPECT_EQ(m.send(s_conn, tx_buf, sizeof(tx_buf)), sizeof(tx_buf));
  EXPECT_EQ(s.send(ss_conn, tx_buf, sizeof(tx_buf)), sizeof(tx_buf));
  bus_m.fault_inject(mcb_if::MCB_ERR_R_ERR);
  bus_m.fault_inject(mcb_if::MCB_ERR_I_ERR);
  wq.sync();
  EXPECT_EQ(m.recv(s_conn, rx_buf, sizeof(rx_buf)), sizeof(rx_buf));
  EXPECT_STREQ((const char *)rx_buf, "Hello, ");
  EXPECT_EQ(s.recv(ss_conn, rx_buf, sizeof(rx_buf)), sizeof(rx_buf));
  EXPECT_STREQ((const char *)rx_buf, "Hello, ");
  auto err = m.get_extra_error(s_conn);
  EXPECT_TRUE(err & (1 << err::LDP_ERR_R_ERROR));
  EXPECT_TRUE(err & (1 << err::LDP_ERR_I_ERROR));
}

TEST_F(test_ldp_sm, async_handle_extra_errors) {
  simu_work_queue wq;

  simu_mcb bus_m(0), bus_s(1);
  ldp_master_impl m(&bus_m, &wq);
  ldp_slave_impl s(&bus_s);

  ldp_master_async_config ma_cfg;
  ldp_slave_async_config sa_cfg;

  ma_cfg.port = 0x10;
  ma_cfg.dst = 0x1;
  ma_cfg.cycle_time = 1;
  ma_cfg.timeout = 3;
  ma_cfg.one_shot = false;
  ma_cfg.preempt = false;
  auto a_conn = m.create(true, &ma_cfg);

  sa_cfg.port = 0x10;
  sa_cfg.max_recv_len = 8;
  auto sa_conn = s.create(true, &sa_cfg);

  uint8_t tx_buf[8] = "Hello, ", rx_buf[8] = "";

  /* For async, it will might receive R_ERROR */
  EXPECT_EQ(m.send(a_conn, tx_buf, sizeof(tx_buf)), sizeof(tx_buf));
  EXPECT_EQ(s.send(sa_conn, tx_buf, sizeof(tx_buf)), sizeof(tx_buf));
  bus_m.fault_inject(mcb_if::MCB_ERR_R_ERR);
  bus_m.fault_inject(mcb_if::MCB_ERR_I_ERR);
  wq.sync();
  EXPECT_EQ(m.recv(a_conn, rx_buf, sizeof(rx_buf)), sizeof(rx_buf));
  EXPECT_STREQ((const char *)rx_buf, "Hello, ");
  EXPECT_EQ(s.recv(sa_conn, rx_buf, sizeof(rx_buf)), sizeof(rx_buf));
  EXPECT_STREQ((const char *)rx_buf, "Hello, ");
  auto err = m.get_extra_error(a_conn);

  /* Since async will spin a few times till there is no more data,
   * the error will be cleaned up and only left with PREV_xxx */
  EXPECT_TRUE(err & (1 << err::LDP_ERR_PREV_R_ERROR));
  EXPECT_TRUE(err & (1 << err::LDP_ERR_PREV_I_ERROR));
}

TEST_F(test_ldp_sm, combined_handle_extra_errors) {
  simu_work_queue wq;

  simu_mcb bus_m(0), bus_s(1);
  ldp_master_impl m(&bus_m, &wq);
  ldp_slave_impl s(&bus_s);

  ldp_master_async_config ma_cfg;
  ldp_slave_async_config sa_cfg;
  ldp_master_sync_config ms_cfg;
  ldp_slave_sync_config ss_cfg;

  ma_cfg.port = 0x10;
  ma_cfg.dst = 0x1;
  ma_cfg.cycle_time = 1;
  ma_cfg.timeout = 3;
  ma_cfg.one_shot = false;
  ma_cfg.preempt = false;
  auto a_conn = m.create(true, &ma_cfg);

  ms_cfg.port = 0x40;
  ms_cfg.dst = 0x1;
  ms_cfg.cycle_time = 100;
  ms_cfg.preempt = false;
  ms_cfg.one_shot = false;
  auto s_conn = m.create(false, &ms_cfg);

  sa_cfg.port = 0x10;
  sa_cfg.max_recv_len = 8;
  auto sa_conn = s.create(true, &sa_cfg);

  ss_cfg.port = 0x40;
  ss_cfg.allow_write = true;
  ss_cfg.max_recv_len = 8;
  auto ss_conn = s.create(false, &ss_cfg);

  uint8_t tx_buf[8] = "Hello, ", rx_buf[8] = "";

  /* For async, it will might receive R_ERROR */
  EXPECT_EQ(m.send(a_conn, tx_buf, sizeof(tx_buf)), sizeof(tx_buf));
  EXPECT_EQ(m.send(s_conn, tx_buf, sizeof(tx_buf)), sizeof(tx_buf));
  EXPECT_EQ(s.send(sa_conn, tx_buf, sizeof(tx_buf)), sizeof(tx_buf));
  EXPECT_EQ(s.send(ss_conn, tx_buf, sizeof(tx_buf)), sizeof(tx_buf));
  bus_m.fault_inject(mcb_if::MCB_ERR_R_ERR);
  bus_m.fault_inject(mcb_if::MCB_ERR_I_ERR);
  wq.sync();
  EXPECT_EQ(m.recv(a_conn, rx_buf, sizeof(rx_buf)), sizeof(rx_buf));
  EXPECT_STREQ((const char *)rx_buf, "Hello, ");
  EXPECT_EQ(m.recv(s_conn, rx_buf, sizeof(rx_buf)), sizeof(rx_buf));
  EXPECT_STREQ((const char *)rx_buf, "Hello, ");
  EXPECT_EQ(s.recv(sa_conn, rx_buf, sizeof(rx_buf)), sizeof(rx_buf));
  EXPECT_STREQ((const char *)rx_buf, "Hello, ");

  auto err_s = m.get_extra_error(s_conn);
  auto err_a = m.get_extra_error(a_conn);
  auto combined = err_s | err_a;

  /* since the R and I error is for bus-leveled, it only shows up in single
   * connection. we can't treat it as a single connection function but rather a
   * bus function */
  uint32_t R_ERROR_MASK =
      (1 << err::LDP_ERR_R_ERROR) | (1 << err::LDP_ERR_PREV_R_ERROR);
  uint32_t I_ERROR_MASK =
      (1 << err::LDP_ERR_I_ERROR) | (1 << err::LDP_ERR_PREV_I_ERROR);
  EXPECT_TRUE(combined & R_ERROR_MASK);
  EXPECT_TRUE(combined & I_ERROR_MASK);
}
