/*
 * SPDX-License-Identifier: Apache-2.0
 * COPYRIGHT (c) 2025 SYSTech Co.
 */
#pragma once

#include "ldp.hpp"

#include <gmock/gmock.h>

struct dummy_cache {
  static inline void rmb() {}
  static inline void wmb() {}
};

struct mock_mempool {
  static std::list<void *> ptrs;

  static inline void *alloc(size_t size) {
    if (alloc_fail) {
      return nullptr;
    }
    auto ptr = new uint8_t[size];
    ptrs.push_back(ptr);
    return ptr;
  }
  static inline void free(void *ptr) {
    auto it = std::find(ptrs.begin(), ptrs.end(), ptr);
    if (it != ptrs.end()) {
      ptrs.erase(it);
      delete[] static_cast<uint8_t *>(ptr);
    }
  }
  static inline void setup() {
    for (auto ptr : ptrs) {
      delete[] static_cast<uint8_t *>(ptr);
    }
    ptrs.clear();
  }
  static inline bool is_empty() { return ptrs.empty(); };

  static inline bool alloc_fail = false;
};

struct mock_work_queue : public systech::cif::work_queue_if {
  MOCK_METHOD(id, enqueue,
              (void (*fn)(void *, void *), void *arg1, void *arg2,
               uint32_t delay),
              (override));
  MOCK_METHOD(void, cancel, (id id), (override));
  MOCK_METHOD(bool, is_ready, (id id), (override));
  virtual void reset(id id, uint32_t delay) override {}
};
struct mock_work_queue_manual : public systech::cif::work_queue_if {
  struct item {
    void (*fn)(void *, void *);
    void *arg1;
    void *arg2;
    uint32_t delay;

    unsigned id;
  };
  using items_t = std::list<item>;
  items_t items;
  unsigned next_id = 0;

  virtual id enqueue(void (*fn)(void *, void *), void *arg1, void *arg2,
                     uint32_t delay) override {
    item i = {fn, arg1, arg2, delay, next_id};
    items.push_back(i);
    return next_id++;
  }
  virtual void cancel(id id) override {
    items.remove_if([id](const item &i) { return i.id == id; });
  }
  virtual void reset(id id, uint32_t delay) override {}
  void sync() {
    for (auto &i : items) {
      i.fn(i.arg1, i.arg2);
    }
  }
  MOCK_METHOD(bool, is_ready, (id id), (override));
};
struct mock_mcb : public systech::cif::mcb_if {
  mock_mcb(uint8_t sid) : sid_(sid) {}
  MOCK_METHOD(void, reset, (uint8_t), (override));
  MOCK_METHOD(void, set_poll_time, (uint16_t timeout), (override));
  MOCK_METHOD(void, config_port,
              (uint8_t port, bool enable, bool w_allow, uint16_t max_rx),
              (override));
  virtual uint8_t *get_rx_buf(uint8_t port) override { return rx_buf_[port]; }
  virtual uint8_t *get_tx_buf(uint8_t port) override { return tx_buf_[port]; }
  MOCK_METHOD(uint16_t, get_rx_len, (uint8_t port), (override));
  MOCK_METHOD(uint16_t, get_tx_len, (uint8_t port), (override));
  MOCK_METHOD(void, set_tx_len, (uint8_t port, uint16_t len), (override));
  MOCK_METHOD(uint32_t, get_status, (), (override));
  MOCK_METHOD(void, clr_status, (uint32_t bits), (override));
  MOCK_METHOD(void, tx, (uint8_t port, uint8_t dst_sid, bool r), (override));
  MOCK_METHOD(bool, has_rx, (uint8_t port), (override));
  MOCK_METHOD(void, clr_rx, (uint8_t port), (override));
  virtual uint8_t get_sid() override { return sid_; }

public: /* for testing */
  using buffer_t = uint8_t[mcb_if::MCB_MAX_FRAME_LEN];

  uint8_t sid_;
  buffer_t rx_buf_[0x100];
  buffer_t tx_buf_[0x100];
  bool data_ready_[0x100];
  bool port_enabled_[0x100];
  bool write_allowed_[0x100];
  uint16_t max_rx_[0x100];
  uint16_t rx_len_[0x100];
  uint16_t tx_len_[0x100];

  uint32_t status_;
};

struct simu_work_queue : public systech::cif::work_queue_if {
  struct item {
    void (*fn)(void *, void *);
    void *arg1;
    void *arg2;
    uint32_t delay;

    unsigned id;
  };
  using items_t = std::list<item>;
  items_t items;
  unsigned next_id = 0;

  virtual id enqueue(void (*fn)(void *, void *), void *arg1, void *arg2,
                     uint32_t delay) override {
    item i{fn, arg1, arg2, delay, next_id};
    items.push_back(i);
    return next_id++;
  }
  virtual void reset(id id, uint32_t delay) override {}
  virtual void cancel(id id) override {
    items.remove_if([id](const item &i) { return i.id == id; });
  }
  void sync() {
    for (auto &i : items) {
      i.fn(i.arg1, i.arg2);
    }
  }
  MOCK_METHOD(bool, is_ready, (id id), (override));
};

struct simu_mcb : public systech::cif::mcb_if {

  enum role { slave, master };
  enum io_mode { dead, fpga_io, ps_io };
  using buffer_t = uint8_t[mcb_if::MCB_MAX_FRAME_LEN];
  inline static constexpr uint8_t max_sid = 0x20;
  inline static constexpr uint8_t max_port = 0x80;

public:
  simu_mcb(uint8_t sid, io_mode m = ps_io) : sid_(sid) { io_mode_[sid_] = m; }

  virtual uint8_t get_sid() override { return sid_; }

  virtual void reset(uint8_t r) override {
    memset(rx_buf_[sid_], 0, sizeof(rx_buf_[sid_]));
    memset(tx_buf_[sid_], 0, sizeof(tx_buf_[sid_]));
    memset(data_ready_[sid_], 0, sizeof(data_ready_[sid_]));
    memset(port_enabled_[sid_], 0, sizeof(port_enabled_[sid_]));
    memset(write_allowed_[sid_], 0, sizeof(write_allowed_[sid_]));
    memset(max_rx_[sid_], 0, sizeof(max_rx_[sid_]));
    memset(rx_len_[sid_], 0, sizeof(rx_len_[sid_]));
    memset(tx_len_[sid_], 0, sizeof(tx_len_[sid_]));
    status_[sid_] = 0;
    role_[sid_] = r == MCB_ROLE_MASTER ? master : slave;
  }

  void fault_inject(mcb_if::mcb_error err) { status_[sid_] |= err; }

  virtual void set_poll_time(uint16_t timeout) override {}

  virtual void config_port(uint8_t port, bool enable, bool w_allow,
                           uint16_t max_rx) override {
    port_enabled_[sid_][port] = enable;
    write_allowed_[sid_][port] = w_allow;
    max_rx_[sid_][port] = max_rx;
  }

  virtual uint8_t *get_rx_buf(uint8_t port) override {
    return rx_buf_[sid_][port];
  }
  virtual uint8_t *get_tx_buf(uint8_t port) override {
    return tx_buf_[sid_][port];
  }
  virtual uint16_t get_rx_len(uint8_t port) override {
    return rx_len_[sid_][port];
  }
  virtual uint16_t get_tx_len(uint8_t port) override {
    return tx_len_[sid_][port];
  }
  virtual void set_tx_len(uint8_t port, uint16_t len) override {
    tx_len_[sid_][port] = len;
  }
  virtual uint32_t get_status() override { return status_[sid_]; }
  virtual void clr_status(uint32_t bits) override { status_[sid_] &= ~bits; }
  virtual bool has_rx(uint8_t port) override { return data_ready_[sid_][port]; }
  virtual void clr_rx(uint8_t port) override {
    data_ready_[sid_][port] = false;
  }
  virtual void tx(uint8_t port, uint8_t dst_sid, bool r) override {
    EXPECT_EQ(role_[sid_], master);
    EXPECT_TRUE(port_enabled_[sid_][port]);

    /* copy self tx buffer into dst rx buffer */
    switch (io_mode_[dst_sid]) {
    case dead:
      break;
    case fpga_io:
    case ps_io:
      if (!port_enabled_[dst_sid][port]) {
        break;
      } else if (!write_allowed_[dst_sid][port] &&
                 tx_len_[sid_][port] > max_rx_[dst_sid][port]) {
        break;
      }
      memcpy(rx_buf_[dst_sid][port], tx_buf_[sid_][port], tx_len_[sid_][port]);
      rx_len_[dst_sid][port] = tx_len_[sid_][port];
      data_ready_[dst_sid][port] = true;

      if (r) {
        status_[dst_sid] |= MCB_ERR_P_ERR;
      }
      break;
    }

    /* copy response into self rx buffer */

    switch (io_mode_[dst_sid]) {
    case dead:
      status_[sid_] |= (MCB_ERR_T_ERR);
      break;
    case fpga_io:
    case ps_io:
      if (!port_enabled_[dst_sid][port]) {
        status_[sid_] |= (MCB_ERR_P_ERR);
        data_ready_[sid_][port] = true;
        rx_len_[sid_][port] = 0;
        break;
      } else if (!write_allowed_[dst_sid][port] &&
                 tx_len_[dst_sid][port] > max_rx_[sid_][port]) {
        status_[sid_] |= (MCB_ERR_P_ERR);
        data_ready_[sid_][port] = true;
        rx_len_[sid_][port] = 0;
        break;
      }
      EXPECT_EQ(write_allowed_[sid_][port], true);
      EXPECT_GE(max_rx_[sid_][port], tx_len_[dst_sid][port]);
      memcpy(rx_buf_[sid_][port], tx_buf_[dst_sid][port],
             tx_len_[dst_sid][port]);
      rx_len_[sid_][port] = tx_len_[dst_sid][port];
      data_ready_[sid_][port] = true;
      break;
    }

#if 0
    printf("tx: port %d, %d->%d\n", port, sid_, dst_sid);
    printf("tx; (%d) status=%x, data_ready=%d, rx_len=%d\n", sid_,
           status_[sid_], data_ready_[sid_][port], rx_len_[sid_][port]);
    printf("tx; (%d) status=%x, data_ready=%d, rx_len=%d\n", dst_sid,
           status_[dst_sid], data_ready_[dst_sid][port],
           rx_len_[dst_sid][port]);
#endif
  }

  static void setup() {
    memset(rx_buf_, 0, sizeof(rx_buf_));
    memset(tx_buf_, 0, sizeof(tx_buf_));
    memset(data_ready_, 0, sizeof(data_ready_));
    memset(port_enabled_, 0, sizeof(port_enabled_));
    memset(write_allowed_, 0, sizeof(write_allowed_));
    memset(max_rx_, 0, sizeof(max_rx_));
    memset(rx_len_, 0, sizeof(rx_len_));
    memset(tx_len_, 0, sizeof(tx_len_));
    memset(status_, 0, sizeof(status_));
    memset(role_, 0, sizeof(role_));
    memset(io_mode_, 0, sizeof(io_mode_));
    for (auto &m : io_mode_) {
      m = dead;
    }
  }

public:
  uint8_t sid_;
  static buffer_t rx_buf_[max_sid][max_port];
  static buffer_t tx_buf_[max_sid][max_port];
  static uint16_t max_rx_[max_sid][max_port];
  static uint16_t rx_len_[max_sid][max_port];
  static uint16_t tx_len_[max_sid][max_port];
  static bool data_ready_[max_sid][max_port];
  static bool port_enabled_[max_sid][max_port];
  static bool write_allowed_[max_sid][max_port];
  static uint32_t status_[max_sid];
  static role role_[max_sid];
  static io_mode io_mode_[max_sid];
};

struct test_ldp_master : public ::testing::Test {
  void SetUp() override { mock_mempool::setup(); }
  void TearDown() override { EXPECT_TRUE(mock_mempool::is_empty()); }
};

struct test_ldp_slave : public ::testing::Test {
  void SetUp() override {}
  void TearDown() override {}
};

struct test_ldp_sm : public ::testing::Test {
  void SetUp() override {
    simu_mcb::setup();
    mock_mempool::setup();
  }
  void TearDown() override { EXPECT_TRUE(mock_mempool::is_empty()); }
};
