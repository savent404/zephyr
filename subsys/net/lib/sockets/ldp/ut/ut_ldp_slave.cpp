/**
 * @file ut_ldp_slave.cpp
 * @author Liao YuanKai(savent_gate@outlook.com)
 * @version 0.1
 * @date 2025-01-07
 * @license This project is CLOSED SOURCE, All Rights Reserved
 * @copyright Copyright (c) 2025 SYSTech Co.
 *
 */
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ldp.hpp"
#include "ut_header.hpp"

using namespace systech::cif;
using namespace testing;
using ldp_slave_impl = ldp_slave<dummy_cache>;

TEST_F(test_ldp_slave, create_sync_conn) {
  mock_mcb mcb(5);

  EXPECT_CALL(mcb, reset(mcb_if::MCB_ROLE_SLAVE)).Times(1);
  ldp_slave_impl s(&mcb);

  ldp_slave_sync_config sync_cfg;
  sync_cfg.port = 0x40;
  sync_cfg.max_recv_len = 32;
  sync_cfg.allow_write = true;

  EXPECT_CALL(mcb, config_port(0x40, true, true, 32)).Times(1);
  EXPECT_CALL(mcb, set_tx_len(0x40, 0)).Times(1);
  auto cfg_conn = s.create(false, &sync_cfg);
  EXPECT_EQ(cfg_conn, 0);

  /* can't create the same connection(sid+port) */
  EXPECT_EQ(s.create(false, &sync_cfg), -ldp_basic::LDP_ERR_CONN_EXIST);

  EXPECT_CALL(mcb, config_port(_, false, false, 0)).Times(1);
}

TEST_F(test_ldp_slave, create_async_conn) {
  mock_mcb mcb(5);

  EXPECT_CALL(mcb, reset(mcb_if::MCB_ROLE_SLAVE)).Times(1);
  ldp_slave_impl s(&mcb);

  ldp_slave_async_config async_cfg;
  async_cfg.port = 0x10;
  async_cfg.max_recv_len = 32;

  EXPECT_CALL(
      mcb, config_port(0x10, true, true, 32 + sizeof(ldp_basic::ldp_a_header)))
      .Times(1);
  EXPECT_CALL(mcb, set_tx_len(0x10, sizeof(ldp_basic::ldp_a_header))).Times(1);
  auto cfg_conn = s.create(true, &async_cfg);
  EXPECT_EQ(cfg_conn, 0);
  /* can't create the same connection(sid+port) */
  EXPECT_EQ(s.create(true, &async_cfg), -ldp_basic::LDP_ERR_CONN_EXIST);
  auto tx_hdr = reinterpret_cast<ldp_basic::ldp_a_header *>(mcb.tx_buf_[0x10]);
  EXPECT_EQ(tx_hdr->xid, ldp_basic::LDP_INITIAL_XID);
  EXPECT_EQ(tx_hdr->rxid, 0);
  EXPECT_EQ(tx_hdr->magic, ldp_basic::LDP_MAGIC);

  EXPECT_CALL(mcb, config_port(_, false, false, 0)).Times(1);
}

TEST_F(test_ldp_slave, create_invalid_conn1) {
  mock_mcb mcb(5);

  EXPECT_CALL(mcb, reset(mcb_if::MCB_ROLE_SLAVE)).Times(1);
  ldp_slave_impl s(&mcb);

  ldp_slave_async_config async_cfg;
  async_cfg.port = 0x10;
  async_cfg.max_recv_len = 0x1000;

  auto cfg_conn = s.create(true, &async_cfg);
  EXPECT_EQ(cfg_conn, -ldp_basic::LDP_ERR_INVALID);
}

TEST_F(test_ldp_slave, create_invalid_conn2) {
  mock_mcb mcb(5);

  EXPECT_CALL(mcb, reset(mcb_if::MCB_ROLE_SLAVE)).Times(1);
  ldp_slave_impl s(&mcb);

  ldp_slave_async_config sync_cfg;
  sync_cfg.port = 0x10;
  sync_cfg.max_recv_len = 0x1000;

  auto cfg_conn = s.create(false, &sync_cfg);
  EXPECT_EQ(cfg_conn, -ldp_basic::LDP_ERR_INVALID);
}

TEST_F(test_ldp_slave, create_invalid_conn3) {
  mock_mcb mcb(5);

  EXPECT_CALL(mcb, reset(mcb_if::MCB_ROLE_SLAVE)).Times(1);
  ldp_slave_impl s(&mcb);

  auto cfg_conn = s.create(true, nullptr);
  EXPECT_EQ(cfg_conn, -ldp_basic::LDP_ERR_INVALID);
}

TEST_F(test_ldp_slave, destroy) {
  mock_mcb mcb(5);

  EXPECT_CALL(mcb, reset(mcb_if::MCB_ROLE_SLAVE)).Times(1);
  ldp_slave_impl s(&mcb);

  ldp_slave_sync_config sync_cfg;
  sync_cfg.port = 0x40;
  sync_cfg.max_recv_len = 32;
  sync_cfg.allow_write = true;

  EXPECT_CALL(mcb, config_port(0x40, true, true, 32)).Times(1);
  EXPECT_CALL(mcb, set_tx_len(0x40, 0)).Times(1);
  auto cfg_conn = s.create(false, &sync_cfg);
  EXPECT_EQ(cfg_conn, 0);

  EXPECT_CALL(mcb, config_port(0x40, false, false, 0)).Times(1);
  auto ret = s.destroy(0);
  EXPECT_EQ(ret, 0);

  /* can't be destroyed again */
  EXPECT_EQ(s.destroy(0), -ldp_basic::LDP_ERR_CONN_NOT_FOUND);
}

TEST_F(test_ldp_slave, destroy_all) {
  mock_mcb mcb(5);

  EXPECT_CALL(mcb, reset(mcb_if::MCB_ROLE_SLAVE)).Times(1);

  ldp_slave_impl s(&mcb);

  ldp_slave_sync_config sync_cfg;
  sync_cfg.port = 0x40;
  sync_cfg.max_recv_len = 32;
  sync_cfg.allow_write = true;

  EXPECT_CALL(mcb, config_port(0x40, true, true, 32)).Times(1);
  EXPECT_CALL(mcb, set_tx_len(0x40, 0)).Times(1);
  auto cfg_conn = s.create(false, &sync_cfg);
  EXPECT_EQ(cfg_conn, 0);

  ldp_slave_async_config async_cfg;
  async_cfg.port = 0x10;
  async_cfg.max_recv_len = 32;

  EXPECT_CALL(
      mcb, config_port(0x10, true, true, 32 + sizeof(ldp_basic::ldp_a_header)))
      .Times(1);
  EXPECT_CALL(mcb, set_tx_len(0x10, sizeof(ldp_basic::ldp_a_header))).Times(1);
  auto cfg_conn2 = s.create(true, &async_cfg);
  EXPECT_EQ(cfg_conn2, 1);

  EXPECT_CALL(mcb, config_port(0x40, false, false, 0)).Times(1);
  EXPECT_CALL(mcb, config_port(0x10, false, false, 0)).Times(1);
  auto ret = s.destroy(0);
  EXPECT_EQ(ret, 0);

  ret = s.destroy(1);
  EXPECT_EQ(ret, 0);
}

TEST_F(test_ldp_slave, sync_send) {
  mock_mcb mcb(5);

  EXPECT_CALL(mcb, reset(mcb_if::MCB_ROLE_SLAVE)).Times(1);
  ldp_slave_impl s(&mcb);

  ldp_slave_sync_config sync_cfg;
  sync_cfg.port = 0x40;
  sync_cfg.max_recv_len = 32;
  sync_cfg.allow_write = true;

  EXPECT_CALL(mcb, config_port(0x40, true, true, 32)).Times(1);
  EXPECT_CALL(mcb, set_tx_len(0x40, 0)).Times(1);
  auto cfg_conn = s.create(false, &sync_cfg);

  EXPECT_CALL(mcb, set_tx_len(0x40, 4)).Times(1);
  EXPECT_CALL(mcb, tx(_, _, _)).Times(0);
  uint8_t tx_buf[32] = {0x01, 0x02, 0x03, 0x04};
  s.send(cfg_conn, tx_buf, 4);

  auto tx_buf_ptr = mcb.tx_buf_[0x40];
  EXPECT_EQ(tx_buf_ptr[0], 0x01);
  EXPECT_EQ(tx_buf_ptr[1], 0x02);
  EXPECT_EQ(tx_buf_ptr[2], 0x03);
  EXPECT_EQ(tx_buf_ptr[3], 0x04);

  EXPECT_CALL(mcb, config_port(0x40, false, false, 0)).Times(1);
}

TEST_F(test_ldp_slave, async_send) {
  int ret;
  mock_mcb mcb(5);

  EXPECT_CALL(mcb, reset(mcb_if::MCB_ROLE_SLAVE)).Times(1);
  ldp_slave_impl s(&mcb);

  ldp_slave_async_config async_cfg;
  async_cfg.port = 0x10;
  async_cfg.max_recv_len = 32;

  EXPECT_CALL(
      mcb, config_port(0x10, true, true, 32 + sizeof(ldp_basic::ldp_a_header)))
      .Times(1);
  EXPECT_CALL(mcb, set_tx_len(0x10, 4)).Times(1);
  auto cfg_conn = s.create(true, &async_cfg);
  EXPECT_EQ(cfg_conn, 0);
  auto tx_hdr = reinterpret_cast<ldp_basic::ldp_a_header *>(mcb.tx_buf_[0x10]);
  EXPECT_EQ(tx_hdr->xid, ldp_basic::LDP_INITIAL_XID);
  EXPECT_EQ(tx_hdr->rxid, 0);
  EXPECT_EQ(tx_hdr->magic, ldp_basic::LDP_MAGIC);

  EXPECT_CALL(mcb, set_tx_len(0x10, 4 + sizeof(ldp_basic::ldp_a_header)))
      .Times(1);
  EXPECT_CALL(mcb, get_rx_len(0x10)).Times(1).WillOnce(Return(0));
  EXPECT_CALL(mcb, get_tx_len(0x10)).Times(1).WillOnce(Return(4));
  EXPECT_CALL(mcb, tx(_, _, _)).Times(0);
  auto rx_hdr = reinterpret_cast<ldp_basic::ldp_a_header *>(mcb.rx_buf_[0x10]);
  rx_hdr->xid = 0xAA;
  uint8_t tx_buf[32] = {0x01, 0x02, 0x03, 0x04};
  ret = s.send(cfg_conn, tx_buf, 4);
  EXPECT_EQ(ret, 4);
  auto tx_buf_ptr = mcb.tx_buf_[0x10];
  tx_hdr = reinterpret_cast<ldp_basic::ldp_a_header *>(tx_buf_ptr);
  EXPECT_EQ(tx_hdr->xid, ldp_basic::LDP_INITIAL_XID + 1);
  EXPECT_EQ(tx_hdr->rxid, 0);
  EXPECT_EQ(tx_hdr->magic, ldp_basic::LDP_MAGIC);
  EXPECT_EQ(tx_buf_ptr[4], 0x01);
  EXPECT_EQ(tx_buf_ptr[5], 0x02);
  EXPECT_EQ(tx_buf_ptr[6], 0x03);
  EXPECT_EQ(tx_buf_ptr[7], 0x04);

  EXPECT_CALL(mcb, config_port(0x10, false, false, 0)).Times(1);
}

TEST_F(test_ldp_slave, invalid_send) {
  mock_mcb mcb(5);

  EXPECT_CALL(mcb, reset(mcb_if::MCB_ROLE_SLAVE)).Times(1);
  ldp_slave_impl s(&mcb);

  ldp_slave_async_config async_cfg;
  async_cfg.port = 0x10;
  async_cfg.max_recv_len = 32;

  EXPECT_CALL(
      mcb, config_port(0x10, true, true, 32 + sizeof(ldp_basic::ldp_a_header)))
      .Times(1);
  EXPECT_CALL(mcb, set_tx_len(0x10, 4)).Times(1);
  auto cfg_conn = s.create(true, &async_cfg);
  EXPECT_EQ(cfg_conn, 0);

  uint8_t tx_buf[32] = {0x01, 0x02, 0x03, 0x04};
  auto ret = s.send(1, tx_buf, 4);
  EXPECT_EQ(ret, -ldp_basic::LDP_ERR_CONN_NOT_FOUND);

  ret = s.send(0, tx_buf, 0);
  EXPECT_EQ(ret, -ldp_basic::LDP_ERR_INVALID);

  ret = s.send(0, nullptr, 4);
  EXPECT_EQ(ret, -ldp_basic::LDP_ERR_INVALID);

  EXPECT_CALL(mcb, config_port(0x10, false, false, 0)).Times(1);
}

TEST_F(test_ldp_slave, sync_recv) {
  mock_mcb mcb(5);

  EXPECT_CALL(mcb, reset(mcb_if::MCB_ROLE_SLAVE)).Times(1);
  ldp_slave_impl s(&mcb);

  ldp_slave_sync_config sync_cfg;
  sync_cfg.port = 0x40;
  sync_cfg.max_recv_len = 32;
  sync_cfg.allow_write = true;

  EXPECT_CALL(mcb, config_port(0x40, true, true, 32)).Times(1);
  EXPECT_CALL(mcb, set_tx_len(0x40, 0)).Times(1);
  auto cfg_conn = s.create(false, &sync_cfg);

  uint8_t rx_buf[32];
  /* Can't recv from invalid conn */
  EXPECT_EQ(s.recv(1, rx_buf, 32), -ldp_basic::LDP_ERR_CONN_NOT_FOUND);
  /* Try to recv empty buffer */
  EXPECT_CALL(mcb, has_rx(0x40)).Times(1).WillOnce(Return(false));
  auto ret = s.recv(cfg_conn, rx_buf, 32);
  EXPECT_EQ(ret, 0);

  /* Now master has sent some data */
  EXPECT_CALL(mcb, has_rx(0x40)).Times(1).WillOnce(Return(true));
  EXPECT_CALL(mcb, clr_rx(0x40)).Times(1);
  EXPECT_CALL(mcb, get_rx_len(0x40)).Times(1).WillOnce(Return(4));
  mcb.rx_buf_[0x40][0] = 0xFF;
  mcb.rx_buf_[0x40][1] = 0xEE;
  mcb.rx_buf_[0x40][2] = 0xDD;
  mcb.rx_buf_[0x40][3] = 0xCC;
  ret = s.recv(cfg_conn, rx_buf, 32);
  EXPECT_EQ(ret, 4);
  EXPECT_EQ(rx_buf[0], 0xFF);
  EXPECT_EQ(rx_buf[1], 0xEE);
  EXPECT_EQ(rx_buf[2], 0xDD);
  EXPECT_EQ(rx_buf[3], 0xCC);

  /* Recv buffer is too small will return error */
  EXPECT_CALL(mcb, has_rx(0x40)).Times(1).WillOnce(Return(true));
  EXPECT_CALL(mcb, get_rx_len(0x40)).Times(1).WillOnce(Return(8));
  ret = s.recv(cfg_conn, rx_buf, 2);
  EXPECT_EQ(ret, -ldp_basic::LDP_ERR_RX_BUF_TOO_SMALL);

  EXPECT_CALL(mcb, config_port(0x40, false, false, 0)).Times(1);
}

TEST_F(test_ldp_slave, async_multi_recv) {
  mock_mcb mcb(5);

  EXPECT_CALL(mcb, reset(mcb_if::MCB_ROLE_SLAVE)).Times(1);
  ldp_slave_impl s(&mcb);

  ldp_slave_sync_config async_cfg;
  async_cfg.port = 0x10;
  async_cfg.max_recv_len = 32;

  EXPECT_CALL(mcb, config_port(0x10, true, true, 36)).Times(1);
  EXPECT_CALL(mcb, set_tx_len(0x10, 4)).Times(1);
  auto cfg_conn = s.create(true, &async_cfg);

  uint8_t rx_buf[32];
  /* Can't recv from invalid conn */
  EXPECT_EQ(s.recv(1, rx_buf, 32), -ldp_basic::LDP_ERR_CONN_NOT_FOUND);
  /* Try to recv empty buffer */
  EXPECT_CALL(mcb, has_rx(0x10)).Times(1).WillOnce(Return(false));
  auto ret = s.recv(cfg_conn, rx_buf, 32);
  EXPECT_EQ(ret, 0);

  /* Now master has sent some data */
  EXPECT_CALL(mcb, has_rx(0x10)).Times(1).WillOnce(Return(true));
  EXPECT_CALL(mcb, get_rx_len(0x10)).Times(1).WillOnce(Return(8));
  EXPECT_CALL(mcb, clr_rx(0x10)).Times(1);
  auto hdr = reinterpret_cast<ldp_basic::ldp_a_header *>(mcb.rx_buf_[0x10]);
  hdr->magic = ldp_basic::LDP_MAGIC;
  hdr->xid = 0xAB;
  hdr->rxid = 0;
  mcb.rx_buf_[0x10][4] = 0xFF;
  mcb.rx_buf_[0x10][5] = 0xEE;
  mcb.rx_buf_[0x10][6] = 0xDD;
  mcb.rx_buf_[0x10][7] = 0xCC;
  ret = s.recv(cfg_conn, rx_buf, 32);
  EXPECT_EQ(ret, 4);
  /* Check if the data is copied correctly */
  auto tx_hdr = reinterpret_cast<ldp_basic::ldp_a_header *>(mcb.tx_buf_[0x10]);
  EXPECT_EQ(tx_hdr->rxid, hdr->xid); /* Acknowledge */
  EXPECT_EQ(rx_buf[0], 0xFF);
  EXPECT_EQ(rx_buf[1], 0xEE);
  EXPECT_EQ(rx_buf[2], 0xDD);
  EXPECT_EQ(rx_buf[3], 0xCC);

  /* Recv buffer is too small will return error */
  EXPECT_CALL(mcb, has_rx(0x10)).Times(1).WillOnce(Return(true));
  EXPECT_CALL(mcb, get_rx_len(0x10)).Times(1).WillOnce(Return(8));
  ret = s.recv(cfg_conn, rx_buf, 2);
  EXPECT_EQ(ret, -ldp_basic::LDP_ERR_RX_BUF_TOO_SMALL);

  /* Recv a invalid packet (no magic or too small) */
  EXPECT_CALL(mcb, has_rx(0x10)).Times(1).WillOnce(Return(true));
  EXPECT_CALL(mcb, get_rx_len(0x10)).Times(1).WillOnce(Return(8));
  hdr = reinterpret_cast<ldp_basic::ldp_a_header *>(mcb.rx_buf_[0x10]);
  hdr->magic = 0;
  ret = s.recv(cfg_conn, rx_buf, 32);
  EXPECT_EQ(ret, -ldp_basic::LDP_ERR_INVALID_ASYNC_PACK);

  EXPECT_CALL(mcb, config_port(0x10, false, false, 0)).Times(1);
}

TEST_F(test_ldp_slave, async_multi_send) {
  int ret;
  mock_mcb mcb(5);

  EXPECT_CALL(mcb, reset(mcb_if::MCB_ROLE_SLAVE)).Times(1);
  ldp_slave_impl s(&mcb);

  ldp_slave_sync_config async_cfg;
  async_cfg.port = 0x10;
  async_cfg.max_recv_len = 32;

  EXPECT_CALL(mcb, config_port(0x10, true, true, 36)).Times(1);
  EXPECT_CALL(mcb, set_tx_len(0x10, 4)).Times(1);
  auto cfg_conn = s.create(true, &async_cfg);

  /* Make sure XID flow is correct:
   * 1. Response.rxid = Request.xid if data is received
   * 2. Response.xid++ if data is send
   */
  uint8_t rx_buf[32];
  uint8_t tx_buf[32] = {0x01, 0x02, 0x03, 0x04};

  /* Try to initiate the send data */
  EXPECT_CALL(mcb, set_tx_len(0x10, 4 + sizeof(ldp_basic::ldp_a_header)))
      .Times(1);
  EXPECT_CALL(mcb, get_rx_len(0x10)).Times(1).WillOnce(Return(0));
  EXPECT_CALL(mcb, get_tx_len(0x10)).Times(1).WillOnce(Return(4));
  EXPECT_CALL(mcb, tx(_, _, _)).Times(0);
  ret = s.send(cfg_conn, tx_buf, 4);
  EXPECT_EQ(ret, 4);

  /* Try to send multiple data, block until master acks */
  EXPECT_CALL(mcb, get_rx_len(0x10)).Times(1).WillOnce(Return(0));
  EXPECT_CALL(mcb, get_tx_len(0x10)).Times(1).WillOnce(Return(8));
  ret = s.send(cfg_conn, tx_buf, 4);
  EXPECT_EQ(ret, -ldp_basic::LDP_ERR_AGAIN);

  /* Now master has sent some data, but xid is not matching */
  EXPECT_CALL(mcb, get_rx_len(0x10)).Times(1).WillOnce(Return(8));
  EXPECT_CALL(mcb, get_tx_len(0x10)).Times(1).WillOnce(Return(8));
  auto rx_hdr = reinterpret_cast<ldp_basic::ldp_a_header *>(mcb.rx_buf_[0x10]);
  rx_hdr->magic = ldp_basic::LDP_MAGIC;
  rx_hdr->xid = ldp_basic::LDP_INITIAL_XID + 1;
  rx_hdr->rxid = 0;
  ret = s.send(cfg_conn, tx_buf, 4);
  EXPECT_EQ(ret, -ldp_basic::LDP_ERR_AGAIN);

  /* Finally, we can send the next data */
  auto tx_hdr = reinterpret_cast<ldp_basic::ldp_a_header *>(mcb.tx_buf_[0x10]);
  rx_hdr->rxid = tx_hdr->xid;
  EXPECT_CALL(mcb, set_tx_len(0x10, 4 + sizeof(ldp_basic::ldp_a_header)))
      .Times(1);
  EXPECT_CALL(mcb, get_rx_len(0x10)).Times(1).WillOnce(Return(8));
  EXPECT_CALL(mcb, get_tx_len(0x10)).Times(1).WillOnce(Return(8));
  EXPECT_CALL(mcb, tx(_, _, _)).Times(0);
  ret = s.send(cfg_conn, tx_buf, 4);
  EXPECT_EQ(ret, 4);

  EXPECT_CALL(mcb, config_port(0x10, false, false, 0)).Times(1);
}

TEST_F(test_ldp_slave, not_required_extra_error) {
  mock_mcb mcb(5);

  EXPECT_CALL(mcb, reset(mcb_if::MCB_ROLE_SLAVE)).Times(1);
  ldp_slave_impl s(&mcb);

  /* Extra error is not required in slave implementation */
  EXPECT_EQ(s.get_extra_error(0), 0);
  s.clr_extra_error(0, 0);
}
