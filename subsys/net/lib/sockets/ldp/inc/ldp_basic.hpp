/**
 * @file ldp_basic.hpp
 * @author Liao YuanKai(savent_gate@outlook.com)
 * @brief Abstract class for CIF/LDP stack
 * @version 0.1
 * @date 2025-01-02
 *
 * @copyright Copyright SYSTech Co. 2025
 * @license This project is CLOSED SOURCE, All Rights Reserved
 *
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>

#include "ldp_utils.hpp"
#include "mcb.hpp"

namespace systech {
namespace cif {

struct ldp_config {
  unsigned port;
};

struct ldp_master_sync_config : ldp_config {
  unsigned dst;        /* destination slave id */
  unsigned cycle_time; /* cycle time in microseconds */
  bool preempt;        /* set R flag, means want to take over the bus */
  bool one_shot;       /* one shot mode, keep sync till receive response */
};

struct ldp_master_async_config : ldp_config {
  unsigned dst;        /* destination slave id */
  unsigned cycle_time; /* cycle time in microseconds */
  unsigned timeout;    /* async timeout in microseconds */
  bool preempt;        /* set R flag, means want to take over the bus */
  bool one_shot;       /* one shot mode, keep sync till receive response */
};

struct ldp_slave_sync_config : ldp_config {
  unsigned max_recv_len; /* max receive length */
  bool allow_write;      /* allow master write to the port */
};

struct ldp_slave_async_config : ldp_config {
  unsigned max_recv_len; /* max receive length */
};

struct ldp_mcb_config {
  uint32_t poll_time; /* poll time in microseconds */
};

struct ldp_basic {
  using conn = int32_t;
  virtual ~ldp_basic() {}

  /**
   * @brief Create sync/async connection
   *
   * @param is_async indicate if the connection is async
   * @param config  connection configuration. For sync connection, it should
   * be
   * @c ldp_master_sync_config or @c ldp_slave_sync_config. For async
   * connection, it should be @c ldp_master_async_config or @c
   * ldp_slave_async_config
   * @return conn connection id, or error code if failed
   * @retval >= 0 connection id
   * @return < 0 error code, see @c ldp_error
   */
  virtual conn create(bool is_async, ldp_config *config) = 0;

  /**
   * @brief destroy connection
   *
   * @param c connection id
   * @return int error code
   * @retval 0 success
   * @return < 0 error code, see @c ldp_error
   */
  virtual int destroy(conn c) = 0;

  /**
   * @brief send data to slave
   *
   * @param c connection id
   * @param buf data buffer
   * @param len data length
   * @return int error code
   * @retval 0 success
   * @return < 0 error code, see @c ldp_error
   */
  virtual int send(conn c, const uint8_t *buf, uint16_t len) = 0;

  /**
   * @brief receive data from slave
   *
   * @param c connection id
   * @param buf data buffer
   * @param len data length
   * @return int error code
   * @retval >= 0 data length
   * @return < 0 error code, see @c ldp_error
   */
  virtual int recv(conn c, uint8_t *buf, uint16_t len) = 0;

  /**
   * @brief get error code
   *
   * @param c connection id
   * @return int error code
   * @retval 0 success
   * @return < 0 error code, see @c ldp_error
   */
  virtual uint32_t get_extra_error(conn c) = 0;

  /**
   * @brief clear error code
   *
   * @param c connection id
   * @param err_bits error bits to clear. see @c ldp_error
   */
  virtual void clr_extra_error(conn c, uint32_t err_bits) = 0;

  /**
   * @brief get error string
   *
   * @param err error code
   * @return const char* error string
   */
  inline static constexpr uint32_t LDP_MAGIC = 0x5151;

  /* initial xid */
  inline static constexpr uint8_t LDP_INITIAL_XID = 0x80;

  enum ldp_error {
    LDP_ERR_OK,
    LDP_ERR_T_ERROR,            /* T_ERROR, no response found */
    LDP_ERR_P_ERROR,            /* P_ERROR, port rejected */
    LDP_ERR_ATIMEOUT,           /* Async timeout */
    LDP_ERR_INVALID_ASYNC_PACK, /* Magic number not match, or length too short*/
    LDP_ERR_INVALID,            /* parameter invalid */
    LDP_ERR_AGAIN,          /* Previous operation not completed, try it later */
    LDP_ERR_CONN_NOT_FOUND, /* Connection id not found */
    LDP_ERR_NOMEM,          /* No more memory */
    LDP_ERR_RX_BUF_TOO_SMALL, /* Rx buffer too small */
    LDP_ERR_CONN_EXIST,       /* Connection already exists */
    __LDP_ERR_IMMEDIATE_MAX,  /* Those errors should be handled immediately by
                                 the caller */

    LDP_ERR_R_ERROR,  /* R_ERROR, redundancy error. One of the frames lost */
    LDP_ERR_I_ERROR,  /* recv invalid frame */
    LDP_ERR_MAY_LOST, /* XID mismatch, means previous packet may be lost */
    LDP_ERR_PREEMPT,  /* Preempted by R=1 frame */
    LDP_ERR_PREV_T_ERROR,  /* T_ERROR happened in previous operation */
    LDP_ERR_PREV_R_ERROR,  /* R_ERROR happened in previous operation */
    LDP_ERR_PREV_I_ERROR,  /* I_ERROR happened in previous operation */
    LDP_ERR_PREV_P_ERROR,  /* P_ERROR happened in previous operation */
    LDP_ERR_PREV_MAY_LOST, /* Previous packet may be lost */
    LDP_ERR_PREV_PREEMPT,  /* R=1 frame receive in previous operation */
    LDP_ERR_PREV_INVALID_ASYNC_PACK, /* Invalid async packet happened in
                                        previous operation */
    LDP_ERR_MAX,
  };
  static_assert(LDP_ERR_MAX < 32, "Too many error codes");
  struct ldp_a_header {
    uint32_t xid : 8;  /* transfer xid */
    uint32_t rxid : 8; /* receiver xid */
    uint32_t magic : 16;
    ldp_a_header() : xid(LDP_INITIAL_XID), rxid(0), magic(LDP_MAGIC) {}
  };
};

} // namespace cif
} // namespace systech
