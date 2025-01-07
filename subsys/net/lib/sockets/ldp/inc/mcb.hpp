/**
 * @file mcb.hpp
 * @author Liao YuanKai(savent_gate@outlook.com)
 * @brief MCB interface for CIF/LDP stack
 * @version 0.1
 * @date 2025-01-02
 * @copyright Copyright SYSTech Co. 2025
 * @license This project is CLOSED SOURCE, All Rights Reserved
 *
 */
#pragma once

#include <cstdint>

namespace systech {
namespace cif {

struct mcb_frame {
  uint8_t dst_sid; /* Destination SID */
  uint8_t src_sid; /* Source SID */
  uint8_t len;     /* Length of the frame */
  uint8_t r : 1;   /* Redundancy flag */
  uint8_t pe : 1;  /* Port error flag */
};

struct mcb_if {
  static const uint32_t MCB_MAX_FRAME_LEN = 0x800;
  enum mcb_error {
    MCB_ERR_OK = 0,
    MCB_ERR_T_ERR = 0x01,   // Timeout
    MCB_ERR_R_ERR = 0x02,     // Redundancy error(lost one of the frames)
    MCB_ERR_I_ERR = 0x04,     // Invalid frame
    MCB_ERR_MUL_FRAME = 0x08, // Multiple frames received
    MCB_ERR_PREEMPT = 0x10,   // Preempted by R=1 frame
    MCB_ERR_P_ERR = 0x20,     // Port rejected
  };

  enum mcb_role {
    MCB_ROLE_MASTER,
    MCB_ROLE_SLAVE,
  };

  virtual ~mcb_if() {}

  /**
   * @brief Reset MCB to initial state
   *
   * @param role MCB_ROLE_MASTER or MCB_ROLE_SLAVE
   * Make sure all the port is disabled, and all the status is cleared
   */
  virtual void reset(uint8_t role) = 0;

  /**
   * @brief Set poll time for MCB
   *
   * @param timeout timeout in ms
   */
  virtual void set_poll_time(uint16_t timeout) = 0;

  /**
   * @brief Config port
   *
   * @param port port number
   * @param enable enable port
   * @param w_allow allow write
   * @param max_rx maximum receive length
   */
  virtual void config_port(uint8_t port, bool enable, bool w_allow,
                           uint16_t max_rx) = 0;

  virtual uint8_t *get_rx_buf(uint8_t port) = 0;
  virtual uint8_t *get_tx_buf(uint8_t port) = 0;
  virtual uint16_t get_rx_len(uint8_t port) = 0;
  virtual uint16_t get_tx_len(uint8_t port) = 0;
  virtual void set_tx_len(uint8_t port, uint16_t len) = 0;

  /**
   * @brief Get the status object
   *
   * @return uint32_t status bits, see @c mcb_error
   */
  virtual uint32_t get_status() = 0;

  /**
   * @brief Clear status bits
   *
   * @param bits bits need to be cleared, see @c mcb_error
   */
  virtual void clr_status(uint32_t bits) = 0;

  /**
   * @brief Trigger a transmission
   *
   * @param port port number
   * @param dst_sid destination SID
   * @param R redundancy flag. If set, the pair needs to give the right of bus
   */
  virtual void tx(uint8_t port, uint8_t dst_sid, bool r) = 0;
  virtual bool has_rx(uint8_t port) = 0;
  virtual void clr_rx(uint8_t port) = 0;

  virtual uint8_t get_sid() = 0;
};

} // namespace cif
} // namespace systech
