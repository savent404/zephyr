/**
 * @file ut_ldp_master.cpp
 * @author Liao YuanKai(savent_gate@outlook.com)
 * @version 0.1
 * @date 2025-01-07
 * @license This project is CLOSED SOURCE, All Rights Reserved
 * @copyright Copyright (c) 2025 SYSTech Co.
 *
 */
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <list>

#include "ldp.hpp"
#include "ut_header.hpp"

using namespace ::testing;
using namespace systech::cif;

std::list<void *> mock_mempool::ptrs;
using ldp_master_impl = ldp_master<mock_mempool, dummy_cache>;

TEST_F(test_ldp_master, create_sync) {
  mock_mcb mcb(0);
  mock_work_queue work_queue;

  EXPECT_CALL(mcb, reset(mcb_if::MCB_ROLE_MASTER)).Times(1);
  EXPECT_CALL(work_queue, enqueue(_, _, _, _)).Times(1);
  ldp_master_impl m(&mcb, &work_queue);

  ldp_master_sync_config sync_cfg;
  sync_cfg.port = 0x40;
  sync_cfg.dst = 0x10;
  sync_cfg.cycle_time = 0;
  sync_cfg.preempt = true;
  sync_cfg.one_shot = false;

  /* invalid configuration */
  EXPECT_EQ(m.create(false, nullptr), -ldp_basic::LDP_ERR_INVALID);

  /* cycle time must > 0 */
  auto cfg_conn = m.create(false, &sync_cfg);
  EXPECT_EQ(cfg_conn, -ldp_basic::LDP_ERR_INVALID);

  sync_cfg.cycle_time = 100;
  cfg_conn = m.create(false, &sync_cfg);
  EXPECT_EQ(cfg_conn, 0);

  /* connection already exist */
  auto cfg_conn1 = m.create(false, &sync_cfg);
  EXPECT_EQ(cfg_conn1, -ldp_basic::LDP_ERR_CONN_EXIST);

  /* can create multiple connection */
  sync_cfg.port = 0x42;
  auto cfg_conn2 = m.create(false, &sync_cfg);
  EXPECT_EQ(cfg_conn2, 1);

  /* cycle time must keep the same */
  sync_cfg.port = 0x41;
  sync_cfg.cycle_time = 200;
  auto cfg_conn3 = m.create(false, &sync_cfg);
  EXPECT_EQ(cfg_conn3, -ldp_basic::LDP_ERR_INVALID);

  /* related resources are freed in the destructor */
  EXPECT_CALL(work_queue, cancel(_)).Times(1);
}

TEST_F(test_ldp_master, create_async) {
  mock_mcb mcb(0);
  mock_work_queue work_queue;

  EXPECT_CALL(mcb, reset(mcb_if::MCB_ROLE_MASTER)).Times(1);
  EXPECT_CALL(work_queue, enqueue(_, _, _, _))
      .Times(3)
      .WillOnce(Return(0))
      .WillOnce(Return(1))
      .WillOnce(Return(2));
  ldp_master_impl m(&mcb, &work_queue);

  /* create invalid async connection */
  EXPECT_EQ(m.create(true, nullptr), -ldp_basic::LDP_ERR_INVALID);

  ldp_master_async_config async_cfg;
  async_cfg.port = 0x40;
  async_cfg.dst = 0x10;
  async_cfg.cycle_time = 100;
  async_cfg.timeout = 1000;
  async_cfg.one_shot = false;
  async_cfg.preempt = false;

  auto cfg_conn = m.create(true, &async_cfg);
  EXPECT_EQ(cfg_conn, 0);

  async_cfg.port = 0x41;
  auto cfg_conn1 = m.create(true, &async_cfg);
  EXPECT_EQ(cfg_conn1, 1);

  /* destroy the connection on purpose */
  EXPECT_CALL(work_queue, cancel(2)).Times(1);
  EXPECT_EQ(m.destroy(cfg_conn1), 0);

  /* create duplicate connection */
  async_cfg.port = 0x40;
  cfg_conn1 = m.create(true, &async_cfg);
  EXPECT_EQ(cfg_conn1, -ldp_basic::LDP_ERR_CONN_EXIST);

  async_cfg.port = 0x41;
  async_cfg.cycle_time = 0;
  auto cfg_conn2 = m.create(true, &async_cfg);
  EXPECT_EQ(cfg_conn2, -ldp_basic::LDP_ERR_INVALID);

  async_cfg.cycle_time = 100;
  async_cfg.timeout = 0;
  auto cfg_conn3 = m.create(true, &async_cfg);
  EXPECT_EQ(cfg_conn3, -ldp_basic::LDP_ERR_INVALID);

  /* related resources are freed in the destructor */
  EXPECT_CALL(work_queue, cancel(0)).Times(1);
  EXPECT_CALL(work_queue, cancel(1)).Times(1);
}

TEST_F(test_ldp_master, create_destroy) {
  mock_mcb mcb(0);
  mock_work_queue_manual work_queue;

  EXPECT_CALL(mcb, reset(mcb_if::MCB_ROLE_MASTER)).Times(1);
  ldp_master_impl m(&mcb, &work_queue);

  ldp_master_async_config async_cfg;
  async_cfg.port = 0x40;
  async_cfg.dst = 0x10;
  async_cfg.cycle_time = 100;
  async_cfg.timeout = 1000;
  async_cfg.one_shot = false;
  async_cfg.preempt = false;

  /* case 1: normal case. create and destroy */
  auto cfg_conn = m.create(true, &async_cfg);
  EXPECT_EQ(cfg_conn, 0);
  EXPECT_EQ(m.destroy(cfg_conn), 0);

  /* case 2: connection not found */
  auto cfg_conn2 = m.destroy(cfg_conn);
  EXPECT_EQ(cfg_conn2, -ldp_basic::LDP_ERR_CONN_NOT_FOUND);

  /* case 3: destroy will free all the buffers(alloc by mempool)
   * received from the slave(rx buffer) and try to send to the slave(tx buffer)
   */
  cfg_conn = m.create(true, &async_cfg);
  EXPECT_EQ(cfg_conn, 1);
  uint8_t rx_buf[0x100], tx_buf[0x100];
  EXPECT_CALL(mcb, config_port(0x40, true, true, mcb_if::MCB_MAX_FRAME_LEN))
      .Times(3);
  EXPECT_CALL(mcb, set_tx_len(0x40, sizeof(ldp_basic::ldp_a_header))).Times(3);
  EXPECT_CALL(mcb, tx(0x40, 0x10, false)).Times(3);
  EXPECT_CALL(mcb, has_rx(0x40)).Times(3).WillRepeatedly(Return(true));
  EXPECT_CALL(mcb, get_rx_len(0x40))
      .Times(3)
      .WillOnce(Return(sizeof(tx_buf) + sizeof(ldp_basic::ldp_a_header)))
      .WillRepeatedly(Return(sizeof(ldp_basic::ldp_a_header)));
  EXPECT_CALL(mcb, clr_rx(0x40)).Times(3);
  EXPECT_CALL(mcb, get_status()).Times(3).WillRepeatedly(Return(0));
  EXPECT_CALL(mcb, clr_status(_)).Times(3);
  auto hdr = reinterpret_cast<ldp_basic::ldp_a_header *>(mcb.get_rx_buf(0x40));
  hdr->magic = ldp_basic::LDP_MAGIC;
  hdr->rxid = 0;
  hdr->xid = 0;
  memcpy(hdr + 1, tx_buf, sizeof(tx_buf));
  work_queue.sync();
  EXPECT_EQ(m.send(cfg_conn, tx_buf, sizeof(tx_buf)), sizeof(tx_buf));
  // EXPECT_EQ(m.recv(cfg_conn, rx_buf, sizeof(rx_buf)), sizeof(tx_buf));
  EXPECT_FALSE(mock_mempool::is_empty());
  EXPECT_EQ(m.destroy(cfg_conn), 0);
  EXPECT_TRUE(mock_mempool::is_empty());

  /* case 4: create and free rx&tx buffer for sync connection */
  mock_mempool::setup();
  ldp_master_sync_config sync_cfg;
  sync_cfg.port = 0x40;
  sync_cfg.dst = 0x10;
  sync_cfg.cycle_time = 100;
  sync_cfg.preempt = false;
  sync_cfg.one_shot = false;
  cfg_conn = m.create(false, &sync_cfg);
  EXPECT_EQ(cfg_conn, 2);
  EXPECT_CALL(mcb, config_port(0x40, true, true, mcb_if::MCB_MAX_FRAME_LEN))
      .Times(1);
  EXPECT_CALL(mcb, set_tx_len(0x40, 0)).Times(1);
  EXPECT_CALL(mcb, tx(0x40, 0x10, false)).Times(1);
  EXPECT_CALL(mcb, has_rx(0x40)).Times(1).WillOnce(Return(true));
  EXPECT_CALL(mcb, get_rx_len(0x40)).Times(1).WillOnce(Return(sizeof(tx_buf)));
  EXPECT_CALL(mcb, clr_rx(0x40)).Times(1);
  EXPECT_CALL(mcb, get_status()).Times(1).WillOnce(Return(0));
  EXPECT_CALL(mcb, clr_status(_)).Times(1);
  work_queue.sync();
  EXPECT_EQ(m.send(cfg_conn, tx_buf, sizeof(tx_buf)), sizeof(tx_buf));
  EXPECT_EQ(m.destroy(cfg_conn), 0);
  EXPECT_TRUE(mock_mempool::is_empty());

  /* case 5: same as case 3, but leave it to the destructor */
  cfg_conn = m.create(true, &async_cfg);
  EXPECT_EQ(cfg_conn, 3);
  EXPECT_CALL(mcb, config_port(0x40, true, true, mcb_if::MCB_MAX_FRAME_LEN))
      .Times(3);
  EXPECT_CALL(mcb, set_tx_len(0x40, sizeof(ldp_basic::ldp_a_header))).Times(3);
  EXPECT_CALL(mcb, tx(0x40, 0x10, false)).Times(3);
  EXPECT_CALL(mcb, has_rx(0x40)).Times(3).WillRepeatedly(Return(true));
  EXPECT_CALL(mcb, get_rx_len(0x40))
      .Times(3)
      .WillOnce(Return(sizeof(tx_buf) + sizeof(ldp_basic::ldp_a_header)))
      .WillRepeatedly(Return(sizeof(ldp_basic::ldp_a_header)));
  EXPECT_CALL(mcb, clr_rx(0x40)).Times(3);
  EXPECT_CALL(mcb, get_status()).Times(3).WillRepeatedly(Return(0));
  EXPECT_CALL(mcb, clr_status(_)).Times(3);
  hdr = reinterpret_cast<ldp_basic::ldp_a_header *>(mcb.get_rx_buf(0x40));
  hdr->magic = ldp_basic::LDP_MAGIC;
  hdr->rxid = 0;
  hdr->xid = 0;
  memcpy(hdr + 1, tx_buf, sizeof(tx_buf));
  work_queue.sync();
  EXPECT_EQ(m.send(cfg_conn, tx_buf, sizeof(tx_buf)), sizeof(tx_buf));
  // EXPECT_EQ(m.recv(cfg_conn, rx_buf, sizeof(rx_buf)), sizeof(tx_buf));
  EXPECT_FALSE(mock_mempool::is_empty());
}

TEST_F(test_ldp_master, sync_rx_tx) {
  mock_mcb mcb(0);
  mock_work_queue_manual work_queue;

  EXPECT_CALL(mcb, reset(mcb_if::MCB_ROLE_MASTER)).Times(1);
  ldp_master_impl m(&mcb, &work_queue);

  ldp_master_sync_config sync_cfg;
  sync_cfg.port = 0x40;
  sync_cfg.dst = 0x10;
  sync_cfg.cycle_time = 100;
  sync_cfg.preempt = false;
  sync_cfg.one_shot = false;
  sync_cfg.preempt = false;

  auto cfg_conn = m.create(false, &sync_cfg);
  EXPECT_EQ(cfg_conn, 0);

  uint8_t rx_buf[0x100];
  int ret = m.recv(cfg_conn, rx_buf, sizeof(rx_buf));
  EXPECT_EQ(ret, -ldp_basic::LDP_ERR_AGAIN);

  EXPECT_CALL(mcb, config_port(0x40, true, true, mcb_if::MCB_MAX_FRAME_LEN))
      .Times(1);
  EXPECT_CALL(mcb, set_tx_len(0x40, 0)).Times(1);
  EXPECT_CALL(mcb, tx(0x40, 0x10, false)).Times(1);
  EXPECT_CALL(mcb, has_rx(0x40)).Times(1).WillOnce(Return(false));
  EXPECT_CALL(mcb, get_status())
      .Times(1)
      .WillOnce(Return(mcb_if::MCB_ERR_TIMEOUT));
  EXPECT_CALL(mcb, clr_status(mcb_if::MCB_ERR_TIMEOUT)).Times(1);
  work_queue.sync();

  /* Tx buffer is empty, so no data is send, and no data is received */
  ret = m.recv(cfg_conn, rx_buf, sizeof(rx_buf));
  EXPECT_EQ(ret, -ldp_basic::LDP_ERR_T_ERROR);

  /* Now we fill the tx buffer, and no data is received */
  uint8_t tx_buf[16] = "Hello, World!";
  ret = m.send(cfg_conn, tx_buf, sizeof(tx_buf));
  EXPECT_EQ(ret, 16);

  EXPECT_CALL(mcb, config_port(0x40, true, true, mcb_if::MCB_MAX_FRAME_LEN))
      .Times(1);
  EXPECT_CALL(mcb, set_tx_len(0x40, 16)).Times(1);
  EXPECT_CALL(mcb, tx(0x40, 0x10, false)).Times(1);
  EXPECT_CALL(mcb, has_rx(0x40)).Times(1).WillOnce(Return(false));
  EXPECT_CALL(mcb, get_status())
      .Times(1)
      .WillOnce(Return(mcb_if::MCB_ERR_TIMEOUT));
  EXPECT_CALL(mcb, clr_rx(0x40)).Times(0);
  EXPECT_CALL(mcb, clr_status(mcb_if::MCB_ERR_TIMEOUT)).Times(1);
  work_queue.sync();

  ret = m.recv(cfg_conn, rx_buf, sizeof(rx_buf));
  EXPECT_EQ(ret, -ldp_basic::LDP_ERR_T_ERROR);

  /* Now slave is ready, go ahead */
  uint8_t *ptr = mcb.get_rx_buf(0x40);
  memcpy(ptr, tx_buf, sizeof(tx_buf));
  mcb.rx_len_[0x40] = sizeof(tx_buf);

  EXPECT_CALL(mcb, config_port(0x40, true, true, mcb_if::MCB_MAX_FRAME_LEN))
      .Times(1);
  EXPECT_CALL(mcb, set_tx_len(0x40, 16)).Times(1);
  EXPECT_CALL(mcb, tx(0x40, 0x10, false)).Times(1);
  EXPECT_CALL(mcb, has_rx(0x40)).Times(1).WillOnce(Return(true));
  EXPECT_CALL(mcb, get_status()).Times(1).WillOnce(Return(0));
  EXPECT_CALL(mcb, get_rx_len(0x40)).Times(1).WillOnce(Return(sizeof(tx_buf)));
  EXPECT_CALL(mcb, clr_rx(0x40)).Times(1);
  EXPECT_CALL(mcb, clr_status(0)).Times(1);
  work_queue.sync();
  ret = m.recv(cfg_conn, rx_buf, 1);
  EXPECT_EQ(ret, -ldp_basic::LDP_ERR_RX_BUF_TOO_SMALL);
  ret = m.recv(cfg_conn, rx_buf, sizeof(rx_buf));
  EXPECT_EQ(ret, sizeof(tx_buf));
  EXPECT_STREQ(reinterpret_cast<char *>(rx_buf), "Hello, World!");

  /* Once again, slave want to extend its tx length. And master should
   * reallocate the tx buffer and accept the new data */
  uint8_t tx_buf1[32] = "Hello, World! Hello, World!";
  EXPECT_CALL(mcb, config_port(0x40, true, true, mcb_if::MCB_MAX_FRAME_LEN))
      .Times(1);
  EXPECT_CALL(mcb, set_tx_len(0x40, _)).Times(1);
  EXPECT_CALL(mcb, tx(0x40, 0x10, false)).Times(1);
  EXPECT_CALL(mcb, has_rx(0x40)).Times(1).WillOnce(Return(true));
  EXPECT_CALL(mcb, get_status()).Times(1).WillOnce(Return(0));
  EXPECT_CALL(mcb, get_rx_len(0x40)).Times(1).WillOnce(Return(sizeof(tx_buf1)));
  EXPECT_CALL(mcb, clr_rx(0x40)).Times(1);
  EXPECT_CALL(mcb, clr_status(0)).Times(1);
  ptr = mcb.get_rx_buf(0x40);
  memcpy(ptr, tx_buf1, sizeof(tx_buf1));
  work_queue.sync();
  ret = m.recv(cfg_conn, rx_buf, sizeof(rx_buf));
  EXPECT_EQ(ret, sizeof(tx_buf1));
  EXPECT_STREQ(reinterpret_cast<char *>(rx_buf), "Hello, World! Hello, World!");
}

TEST_F(test_ldp_master, sync_with_preempt) {
  mock_mcb mcb(0);
  mock_work_queue_manual work_queue;

  EXPECT_CALL(mcb, reset(mcb_if::MCB_ROLE_MASTER)).Times(1);

  ldp_master_impl m(&mcb, &work_queue);

  ldp_master_sync_config sync_cfg;
  sync_cfg.port = 0x40;
  sync_cfg.dst = 0x10;
  sync_cfg.cycle_time = 100;
  sync_cfg.preempt = false;
  sync_cfg.one_shot = false;
  auto cfg_conn = m.create(false, &sync_cfg);
  EXPECT_EQ(cfg_conn, 0);

  uint8_t rx_buf[10], tx_buf[0x100];
  EXPECT_CALL(mcb, config_port(0x40, true, true, mcb_if::MCB_MAX_FRAME_LEN))
      .Times(1);
  EXPECT_CALL(mcb, set_tx_len(0x40, 10)).Times(1);
  EXPECT_CALL(mcb, tx(0x40, 0x10, false)).Times(1);
  EXPECT_CALL(mcb, has_rx(0x40)).Times(1).WillOnce(Return(true));
  EXPECT_CALL(mcb, get_rx_len(0x40)).Times(1).WillOnce(Return(10));
  EXPECT_CALL(mcb, clr_rx(0x40)).Times(1);
  EXPECT_CALL(mcb, get_status())
      .Times(1)
      .WillOnce(Return(mcb_if::MCB_ERR_PREEMPT));
  EXPECT_CALL(mcb, clr_status(mcb_if::MCB_ERR_PREEMPT)).Times(1);
  {
    // mempool will fail to allocate the buffer
    mock_mempool::alloc_fail = true;
    EXPECT_EQ(m.send(cfg_conn, tx_buf, 10), -ldp_basic::LDP_ERR_NOMEM);
    mock_mempool::alloc_fail = false;
  }
  EXPECT_EQ(m.send(cfg_conn, tx_buf, 10), 10);
  work_queue.sync();
  EXPECT_EQ(m.recv(cfg_conn, rx_buf, sizeof(rx_buf)), 10);

  auto extra_err = m.get_extra_error(cfg_conn);
  EXPECT_TRUE(extra_err & (1 << ldp_basic::LDP_ERR_PREEMPT));

  /* clear the error */
  m.clr_extra_error(cfg_conn, 1 << ldp_basic::LDP_ERR_PREEMPT);
  EXPECT_EQ(m.get_extra_error(cfg_conn) & (1 << ldp_basic::LDP_ERR_PREEMPT), 0);
}

TEST_F(test_ldp_master, async_rx_tx) {
  mock_mcb mcb(0);
  mock_work_queue_manual work_queue;

  EXPECT_CALL(mcb, reset(mcb_if::MCB_ROLE_MASTER)).Times(1);
  ldp_master_impl m(&mcb, &work_queue);

  ldp_master_async_config async_cfg;
  async_cfg.port = 0x10;
  async_cfg.dst = 0x10;
  async_cfg.cycle_time = 100;
  async_cfg.timeout = 1000;
  async_cfg.one_shot = false;
  async_cfg.preempt = false;

  auto cfg_conn = m.create(true, &async_cfg);
  EXPECT_EQ(cfg_conn, 0);

  uint8_t rx_buf[0x100];
  int ret = m.recv(cfg_conn, rx_buf, sizeof(rx_buf));
  EXPECT_EQ(ret, -ldp_basic::LDP_ERR_AGAIN);

  /* will send emtpy request to gain the response */
  EXPECT_CALL(mcb, config_port(0x10, true, true, mcb_if::MCB_MAX_FRAME_LEN))
      .Times(1);
  EXPECT_CALL(mcb, set_tx_len(0x10, sizeof(ldp_basic::ldp_a_header))).Times(1);
  EXPECT_CALL(mcb, tx(0x10, 0x10, false)).Times(1);
  EXPECT_CALL(mcb, has_rx(0x10)).Times(1).WillOnce(Return(false));
  EXPECT_CALL(mcb, get_status())
      .Times(1)
      .WillOnce(Return(mcb_if::MCB_ERR_TIMEOUT));
  EXPECT_CALL(mcb, clr_rx(0x10)).Times(0);
  EXPECT_CALL(mcb, clr_status(mcb_if::MCB_ERR_TIMEOUT)).Times(1);
  work_queue.sync();

  /* Tx buffer is empty, so no data is send, and no data is received */
  ret = m.recv(cfg_conn, rx_buf, sizeof(rx_buf));
  EXPECT_EQ(ret, -ldp_basic::LDP_ERR_T_ERROR);

  /* Now we fill the tx buffer, and no data is received */
  uint8_t tx_buf[16] = "Hello, World!";
  ret = m.send(cfg_conn, tx_buf, sizeof(tx_buf));
  EXPECT_EQ(ret, 16);

  EXPECT_CALL(mcb, config_port(0x10, true, true, mcb_if::MCB_MAX_FRAME_LEN))
      .Times(1);
  EXPECT_CALL(mcb, set_tx_len(0x10, 16 + sizeof(ldp_basic::ldp_a_header)))
      .Times(1);
  EXPECT_CALL(mcb, tx(0x10, 0x10, false)).Times(1);
  EXPECT_CALL(mcb, has_rx(0x10)).Times(1).WillOnce(Return(false));
  EXPECT_CALL(mcb, get_status())
      .Times(1)
      .WillOnce(Return(mcb_if::MCB_ERR_TIMEOUT));
  EXPECT_CALL(mcb, clr_rx(0x10)).Times(0);
  EXPECT_CALL(mcb, clr_status(mcb_if::MCB_ERR_TIMEOUT)).Times(1);
  work_queue.sync();

  ret = m.recv(cfg_conn, rx_buf, sizeof(rx_buf));
  EXPECT_EQ(ret, -ldp_basic::LDP_ERR_T_ERROR);

  /* Now slave is ready, responed with the data */
  uint8_t *ptr = mcb.get_rx_buf(0x10);
  auto rx_hdr = reinterpret_cast<ldp_basic::ldp_a_header *>(ptr);
  auto tx_hdr = reinterpret_cast<ldp_basic::ldp_a_header *>(mcb.tx_buf_[0x10]);
  rx_hdr->magic = ldp_basic::LDP_MAGIC;
  rx_hdr->rxid = tx_hdr->xid;
  rx_hdr->xid = 0;
  memcpy(ptr + sizeof(ldp_basic::ldp_a_header), tx_buf, sizeof(tx_buf));

  EXPECT_CALL(mcb, config_port(0x10, true, true, mcb_if::MCB_MAX_FRAME_LEN))
      .Times(3);
  EXPECT_CALL(mcb, set_tx_len(0x10, 16 + sizeof(ldp_basic::ldp_a_header)))
      .Times(1);
  EXPECT_CALL(mcb, set_tx_len(0x10, sizeof(ldp_basic::ldp_a_header))).Times(2);
  EXPECT_CALL(mcb, tx(0x10, 0x10, false)).Times(3);
  EXPECT_CALL(mcb, has_rx(0x10)).Times(3).WillRepeatedly(Return(true));
  EXPECT_CALL(mcb, get_status()).Times(3).WillRepeatedly(Return(0));
  EXPECT_CALL(mcb, get_rx_len(0x10))
      .Times(3)
      .WillRepeatedly(Return(sizeof(tx_buf) + sizeof(ldp_basic::ldp_a_header)));
  EXPECT_CALL(mcb, clr_rx(0x10)).Times(3);
  EXPECT_CALL(mcb, clr_status(_)).Times(3);
  work_queue.sync();

  ret = m.recv(cfg_conn, rx_buf, 1);
  EXPECT_EQ(ret, -ldp_basic::LDP_ERR_RX_BUF_TOO_SMALL);
  ret = m.recv(cfg_conn, rx_buf, sizeof(rx_buf));
  EXPECT_EQ(ret, sizeof(tx_buf));
  EXPECT_STREQ(reinterpret_cast<char *>(rx_buf), "Hello, World!");
}

TEST_F(test_ldp_master, async_with_preempt) {
  mock_mcb mcb(0);
  mock_work_queue_manual work_queue;

  EXPECT_CALL(mcb, reset(mcb_if::MCB_ROLE_MASTER)).Times(1);
  ldp_master_impl m(&mcb, &work_queue);

  ldp_master_async_config async_cfg;
  async_cfg.port = 0x10;
  async_cfg.dst = 0x10;
  async_cfg.cycle_time = 100;
  async_cfg.timeout = 1000;
  async_cfg.one_shot = false;
  async_cfg.preempt = false;

  auto cfg_conn = m.create(true, &async_cfg);
  EXPECT_EQ(cfg_conn, 0);

  uint8_t rx_buf[0x100];
  int ret = m.recv(cfg_conn, rx_buf, sizeof(rx_buf));
  EXPECT_EQ(ret, -ldp_basic::LDP_ERR_AGAIN);

  /* will send emtpy request to gain the response */
  EXPECT_CALL(mcb, config_port(0x10, true, true, mcb_if::MCB_MAX_FRAME_LEN))
      .Times(1);
  EXPECT_CALL(mcb, set_tx_len(0x10, sizeof(ldp_basic::ldp_a_header))).Times(1);
  EXPECT_CALL(mcb, tx(0x10, 0x10, false)).Times(1);
  EXPECT_CALL(mcb, has_rx(0x10)).Times(1).WillOnce(Return(false));
  EXPECT_CALL(mcb, get_status())
      .Times(1)
      .WillOnce(Return(mcb_if::MCB_ERR_TIMEOUT));
  EXPECT_CALL(mcb, clr_rx(0x10)).Times(0);
  EXPECT_CALL(mcb, clr_status(mcb_if::MCB_ERR_TIMEOUT)).Times(1);
  work_queue.sync();

  /* Tx buffer is empty, so no data is send, and no data is received */
  ret = m.recv(cfg_conn, rx_buf, sizeof(rx_buf));
  EXPECT_EQ(ret, -ldp_basic::LDP_ERR_T_ERROR);

  /* Now we fill the tx buffer, and no data is received */
  uint8_t tx_buf[16] = "Hello, World!";
  {
    // mempool will fail to allocate the buffer
    mock_mempool::alloc_fail = true;
    EXPECT_EQ(m.send(cfg_conn, tx_buf, sizeof(tx_buf)),
              -ldp_basic::LDP_ERR_NOMEM);
    mock_mempool::alloc_fail = false;
  }
  ret = m.send(cfg_conn, tx_buf, sizeof(tx_buf));
  EXPECT_EQ(ret, 16);

  EXPECT_CALL(mcb, config_port(0x10, true, true, mcb_if::MCB_MAX_FRAME_LEN))
      .Times(1);
  EXPECT_CALL(mcb, set_tx_len(0x10, 16 + sizeof(ldp_basic::ldp_a_header)))
      .Times(1);
  EXPECT_CALL(mcb, tx(0x10, 0x10, false)).Times(1);
  EXPECT_CALL(mcb, has_rx(0x10)).Times(1).WillOnce(Return(false));
  EXPECT_CALL(mcb, get_status())
      .Times(1)
      .WillOnce(Return(mcb_if::MCB_ERR_TIMEOUT));
  EXPECT_CALL(mcb, clr_rx(0x10)).Times(0);
  EXPECT_CALL(mcb, clr_status(mcb_if::MCB_ERR_TIMEOUT)).Times(1);
  work_queue.sync();

  ret = m.recv(cfg_conn, rx_buf, sizeof(rx_buf));
  EXPECT_EQ(ret, -ldp_basic::LDP_ERR_T_ERROR);

  /* Now slave is ready, responed with the data */
  uint8_t *ptr = mcb.get_rx_buf(0x10);
  auto rx_hdr = reinterpret_cast<ldp_basic::ldp_a_header *>(ptr);
  auto tx_hdr = reinterpret_cast<ldp_basic::ldp_a_header *>(mcb.tx_buf_[0x10]);
  rx_hdr->magic = ldp_basic::LDP_MAGIC;
  rx_hdr->rxid = tx_hdr->xid;
  rx_hdr->xid = 0;
  memcpy(ptr + sizeof(ldp_basic::ldp_a_header), tx_buf, sizeof(tx_buf));

  EXPECT_CALL(mcb, config_port(0x10, true, true, mcb_if::MCB_MAX_FRAME_LEN))
      .Times(3);
  EXPECT_CALL(mcb, set_tx_len(0x10, 16 + sizeof(ldp_basic::ldp_a_header)))
      .Times(1);
  EXPECT_CALL(mcb, set_tx_len(0x10, sizeof(ldp_basic::ldp_a_header))).Times(2);
  EXPECT_CALL(mcb, tx(0x10, 0x10, false)).Times(3);
  EXPECT_CALL(mcb, has_rx(0x10)).Times(3).WillRepeatedly(Return(true));
  EXPECT_CALL(mcb, get_status())
      .Times(3)
      .WillRepeatedly(Return(mcb_if::MCB_ERR_PREEMPT));
  EXPECT_CALL(mcb, get_rx_len(0x10))
      .Times(3)
      .WillRepeatedly(Return(sizeof(tx_buf) + sizeof(ldp_basic::ldp_a_header)));
  EXPECT_CALL(mcb, clr_rx(0x10)).Times(3);
  EXPECT_CALL(mcb, clr_status(_)).Times(3);
  work_queue.sync();

  ret = m.recv(cfg_conn, rx_buf, sizeof(rx_buf));
  EXPECT_EQ(ret, sizeof(tx_buf));
  EXPECT_STREQ(reinterpret_cast<char *>(rx_buf), "Hello, World!");

  auto extra_err = m.get_extra_error(cfg_conn);
  EXPECT_TRUE(extra_err & (1 << ldp_basic::LDP_ERR_PREEMPT));
  m.clr_extra_error(cfg_conn, 1 << ldp_basic::LDP_ERR_PREEMPT);
  EXPECT_EQ(m.get_extra_error(cfg_conn) & (1 << ldp_basic::LDP_ERR_PREEMPT), 0);
}

TEST_F(test_ldp_master, invalid_check) {
  mock_mcb mcb(0);
  mock_work_queue_manual work_queue;

  EXPECT_CALL(mcb, reset(mcb_if::MCB_ROLE_MASTER)).Times(1);
  ldp_master_impl m(&mcb, &work_queue);

  ldp_master_sync_config sync_cfg;
  sync_cfg.port = 0x40;
  sync_cfg.dst = 0x10;
  sync_cfg.cycle_time = 100;
  sync_cfg.preempt = false;
  sync_cfg.one_shot = false;
  sync_cfg.preempt = false;

  auto conn = m.create(false, &sync_cfg);
  EXPECT_EQ(conn, 0);

  /* case 1: send /recv invalid connection */
  uint8_t tx_buf[16] = "Hello, World!";
  int ret = m.send(1, tx_buf, sizeof(tx_buf));
  EXPECT_EQ(ret, -ldp_basic::LDP_ERR_CONN_NOT_FOUND);
  ret = m.recv(1, tx_buf, sizeof(tx_buf));
  EXPECT_EQ(ret, -ldp_basic::LDP_ERR_CONN_NOT_FOUND);

  /* case 2: invalid get/set extra errors */
  auto extra_err = m.get_extra_error(1);
  EXPECT_EQ(extra_err, (1 << ldp_basic::LDP_ERR_CONN_NOT_FOUND));
  m.clr_extra_error(1, 1 << ldp_basic::LDP_ERR_CONN_NOT_FOUND);
}
