/*
 * Copyright (c) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "flash_cadence_qspi_nor_ll.h"

#include <string.h>

#include <zephyr/logging/log.h>

#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>

LOG_MODULE_REGISTER(flash_cadence_ll, CONFIG_FLASH_LOG_LEVEL);

static struct k_spinlock cad_qspi_lock;

static inline k_spinlock_key_t cad_qspi_critical_enter(void)
{
	return k_spin_lock(&cad_qspi_lock);
}

static inline void cad_qspi_critical_exit(k_spinlock_key_t key)
{
	k_spin_unlock(&cad_qspi_lock, key);
}

int cad_qspi_idle(struct cad_qspi_params *cad_params)
{
	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	return (sys_read32(cad_params->reg_base + CAD_QSPI_CFG) & CAD_QSPI_CFG_IDLE) >> 31;
}

int cad_qspi_set_baudrate_div(struct cad_qspi_params *cad_params, uint32_t div)
{
	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	if (div > 0xf) {
		return CAD_INVALID;
	}

	sys_clear_bits(cad_params->reg_base + CAD_QSPI_CFG, ~CAD_QSPI_CFG_BAUDDIV_MSK);

	sys_set_bits(cad_params->reg_base + CAD_QSPI_CFG, CAD_QSPI_CFG_BAUDDIV(div));

	return 0;
}

int cad_qspi_configure_dev_size(struct cad_qspi_params *cad_params, uint32_t addr_bytes,
				uint32_t bytes_per_dev, uint32_t bytes_per_block)
{
	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	sys_write32(CAD_QSPI_DEVSZ_ADDR_BYTES(addr_bytes) |
			    CAD_QSPI_DEVSZ_BYTES_PER_PAGE(bytes_per_dev) |
			    CAD_QSPI_DEVSZ_BYTES_PER_BLOCK(bytes_per_block),
		    cad_params->reg_base + CAD_QSPI_DEVSZ);
	return 0;
}

int cad_qspi_set_read_config(struct cad_qspi_params *cad_params, uint32_t opcode,
			     uint32_t instr_type, uint32_t addr_type, uint32_t data_type,
			     uint32_t mode_bit, uint32_t dummy_clk_cycle)
{
	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	sys_write32(CAD_QSPI_DEV_OPCODE(opcode) | CAD_QSPI_DEV_INST_TYPE(instr_type) |
			    CAD_QSPI_DEV_ADDR_TYPE(addr_type) | CAD_QSPI_DEV_DATA_TYPE(data_type) |
			    CAD_QSPI_DEV_MODE_BIT(mode_bit) |
			    CAD_QSPI_DEV_DUMMY_CLK_CYCLE(dummy_clk_cycle),
		    cad_params->reg_base + CAD_QSPI_DEVRD);

	return 0;
}

int cad_qspi_set_write_config(struct cad_qspi_params *cad_params, uint32_t opcode,
			      uint32_t addr_type, uint32_t data_type, uint32_t dummy_clk_cycle)
{
	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	sys_write32(CAD_QSPI_DEV_OPCODE(opcode) | CAD_QSPI_DEV_ADDR_TYPE(addr_type) |
			    CAD_QSPI_DEV_DATA_TYPE(data_type) |
			    CAD_QSPI_DEV_DUMMY_CLK_CYCLE(dummy_clk_cycle),
		    cad_params->reg_base + CAD_QSPI_DEVWR);

	return 0;
}

int cad_qspi_timing_config(struct cad_qspi_params *cad_params, uint32_t clkphase, uint32_t clkpol,
			   uint32_t csda, uint32_t csdads, uint32_t cseot, uint32_t cssot,
			   uint32_t rddatacap)
{
	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	uint32_t cfg = sys_read32(cad_params->reg_base + CAD_QSPI_CFG);

	cfg &= CAD_QSPI_CFG_SELCLKPHASE_CLR_MSK & CAD_QSPI_CFG_SELCLKPOL_CLR_MSK;
	cfg |= CAD_QSPI_SELCLKPHASE(clkphase) | CAD_QSPI_SELCLKPOL(clkpol);

	sys_write32(cfg, cad_params->reg_base + CAD_QSPI_CFG);

	sys_write32(CAD_QSPI_DELAY_CSSOT(cssot) | CAD_QSPI_DELAY_CSEOT(cseot) |
			    CAD_QSPI_DELAY_CSDADS(csdads) | CAD_QSPI_DELAY_CSDA(csda),
		    cad_params->reg_base + CAD_QSPI_DELAY);

	return 0;
}

int cad_qspi_stig_cmd_helper(struct cad_qspi_params *cad_params, int cs, uint32_t cmd)
{
	uint32_t count = 0;

	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	/* chip select */
	sys_write32((sys_read32(cad_params->reg_base + CAD_QSPI_CFG) & CAD_QSPI_CFG_CS_MSK) |
			    CAD_QSPI_CFG_CS(cs),
		    cad_params->reg_base + CAD_QSPI_CFG);

	sys_write32(cmd, cad_params->reg_base + CAD_QSPI_FLASHCMD);
	sys_write32(cmd | CAD_QSPI_FLASHCMD_EXECUTE, cad_params->reg_base + CAD_QSPI_FLASHCMD);

	do {
		uint32_t reg = sys_read32(cad_params->reg_base + CAD_QSPI_FLASHCMD);

		if (!(reg & CAD_QSPI_FLASHCMD_EXECUTE_STAT)) {
			break;
		}
		count++;
	} while (count < CAD_QSPI_COMMAND_TIMEOUT);

	if (count >= CAD_QSPI_COMMAND_TIMEOUT) {
		LOG_ERR("Error sending QSPI command %x, timed out\n", cmd);
		return CAD_QSPI_ERROR;
	}

	return 0;
}

int cad_qspi_stig_cmd(struct cad_qspi_params *cad_params, uint32_t opcode, uint32_t dummy)
{

	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	if (dummy > ((1 << CAD_QSPI_FLASHCMD_NUM_DUMMYBYTES_MAX) - 1)) {
		LOG_ERR("Faulty dummy bytes\n");
		return -1;
	}

	return cad_qspi_stig_cmd_helper(cad_params, cad_params->cad_qspi_cs,
					CAD_QSPI_FLASHCMD_OPCODE(opcode) |
						CAD_QSPI_FLASHCMD_NUM_DUMMYBYTES(dummy));
}

int cad_qspi_stig_read_cmd(struct cad_qspi_params *cad_params, uint32_t opcode, uint32_t dummy,
			   uint32_t num_bytes, uint32_t *output)
{
	if (dummy > ((1 << CAD_QSPI_FLASHCMD_NUM_DUMMYBYTES_MAX) - 1)) {
		LOG_ERR("Faulty dummy byes\n");
		return -1;
	}

	if ((num_bytes > 8) || (num_bytes == 0)) {
		return -1;
	}

	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	uint32_t cmd = CAD_QSPI_FLASHCMD_OPCODE(opcode) | CAD_QSPI_FLASHCMD_ENRDDATA(1) |
		       CAD_QSPI_FLASHCMD_NUMRDDATABYTES(num_bytes - 1) |
		       CAD_QSPI_FLASHCMD_ENCMDADDR(0) | CAD_QSPI_FLASHCMD_ENMODEBIT(0) |
		       CAD_QSPI_FLASHCMD_NUMADDRBYTES(0) | CAD_QSPI_FLASHCMD_ENWRDATA(0) |
		       CAD_QSPI_FLASHCMD_NUMWRDATABYTES(0) | CAD_QSPI_FLASHCMD_NUMDUMMYBYTES(dummy);

	if (cad_qspi_stig_cmd_helper(cad_params, cad_params->cad_qspi_cs, cmd)) {
		LOG_ERR("failed to send stig cmd\n");
		return -1;
	}

	output[0] = sys_read32(cad_params->reg_base + CAD_QSPI_FLASHCMD_RDDATA0);

	if (num_bytes > 4) {
		output[1] = sys_read32(cad_params->reg_base + CAD_QSPI_FLASHCMD_RDDATA1);
	}

	return 0;
}

int cad_qspi_stig_wr_cmd(struct cad_qspi_params *cad_params, uint32_t opcode, uint32_t dummy,
			 uint32_t num_bytes, uint32_t *input)
{
	if (dummy > ((1 << CAD_QSPI_FLASHCMD_NUM_DUMMYBYTES_MAX) - 1)) {
		LOG_ERR("Faulty dummy byes\n");
		return -1;
	}

	if ((num_bytes > 8) || (num_bytes == 0)) {
		return -1;
	}

	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	uint32_t cmd = CAD_QSPI_FLASHCMD_OPCODE(opcode) | CAD_QSPI_FLASHCMD_ENRDDATA(0) |
		       CAD_QSPI_FLASHCMD_NUMRDDATABYTES(0) | CAD_QSPI_FLASHCMD_ENCMDADDR(0) |
		       CAD_QSPI_FLASHCMD_ENMODEBIT(0) | CAD_QSPI_FLASHCMD_NUMADDRBYTES(0) |
		       CAD_QSPI_FLASHCMD_ENWRDATA(1) |
		       CAD_QSPI_FLASHCMD_NUMWRDATABYTES(num_bytes - 1) |
		       CAD_QSPI_FLASHCMD_NUMDUMMYBYTES(dummy);

	sys_write32(input[0], cad_params->reg_base + CAD_QSPI_FLASHCMD_WRDATA0);

	if (num_bytes > 4) {
		sys_write32(input[1], cad_params->reg_base + CAD_QSPI_FLASHCMD_WRDATA1);
	}

	return cad_qspi_stig_cmd_helper(cad_params, cad_params->cad_qspi_cs, cmd);
}

int cad_qspi_stig_addr_cmd(struct cad_qspi_params *cad_params, uint32_t opcode, uint32_t dummy,
			   uint32_t addr)
{
	uint32_t cmd;

	if (dummy > ((1 << CAD_QSPI_FLASHCMD_NUM_DUMMYBYTES_MAX) - 1)) {
		return -1;
	}

	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	cmd = CAD_QSPI_FLASHCMD_OPCODE(opcode) | CAD_QSPI_FLASHCMD_NUMDUMMYBYTES(dummy) |
	      CAD_QSPI_FLASHCMD_ENCMDADDR(1) | CAD_QSPI_FLASHCMD_NUMADDRBYTES(2);

	sys_write32(addr, cad_params->reg_base + CAD_QSPI_FLASHCMD_ADDR);

	return cad_qspi_stig_cmd_helper(cad_params, cad_params->cad_qspi_cs, cmd);
}

int cad_qspi_device_bank_select(struct cad_qspi_params *cad_params, uint32_t bank)
{
	int status = 0;

	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	status = cad_qspi_stig_cmd(cad_params, CAD_QSPI_STIG_OPCODE_WREN, 0);

	if (status != 0) {
		return status;
	}

	status = cad_qspi_stig_wr_cmd(cad_params, CAD_QSPI_STIG_OPCODE_WREN_EXT_REG, 0, 1, &bank);

	if (status != 0) {
		return status;
	}

	return cad_qspi_stig_cmd(cad_params, CAD_QSPI_STIG_OPCODE_WRDIS, 0);
}

int cad_qspi_device_status(struct cad_qspi_params *cad_params, uint32_t *status)
{
	return cad_qspi_stig_read_cmd(cad_params, CAD_QSPI_STIG_OPCODE_RDSR, 0, 1, status);
}

#if CAD_QSPI_MICRON_N25Q_SUPPORT
int cad_qspi_n25q_enable(struct cad_qspi_params *cad_params)
{
	cad_qspi_set_read_config(cad_params, QSPI_FAST_READ, CAD_QSPI_INST_SINGLE,
				 CAD_QSPI_ADDR_FASTREAD, CAT_QSPI_ADDR_SINGLE_IO, 1, 0);
	cad_qspi_set_write_config(cad_params, QSPI_WRITE, 0, 0, 0);

	return 0;
}

int cad_qspi_n25q_wait_for_program_and_erase(struct cad_qspi_params *cad_params, int program_only)
{
	uint32_t status, flag_sr;
	int count = 0;

	while (count < CAD_QSPI_COMMAND_TIMEOUT) {
		status = cad_qspi_device_status(cad_params, &flag_sr);
		if (status != 0) {
			LOG_ERR("Error getting device status\n");
			return -1;
		}
		if (!CAD_QSPI_STIG_SR_BUSY(flag_sr)) {
			break;
		}
		count++;
	}

	if (count >= CAD_QSPI_COMMAND_TIMEOUT) {
		LOG_ERR("Timed out waiting for idle\n");
		return -1;
	}

	count = 0;

	while (count < CAD_QSPI_COMMAND_TIMEOUT) {
		status = cad_qspi_stig_read_cmd(cad_params, CAD_QSPI_STIG_OPCODE_RDFLGSR, 0, 1,
						&flag_sr);
		if (status != 0) {
			LOG_ERR("Error waiting program and erase.\n");
			return status;
		}

		if ((program_only && CAD_QSPI_STIG_FLAGSR_PROGRAMREADY(flag_sr)) ||
		    (!program_only && CAD_QSPI_STIG_FLAGSR_ERASEREADY(flag_sr))) {
			break;
		}
	}

#if FLASH_CAD_FLASH_QSPI_NOR
	uint8_t is_program_error = false;
	uint8_t is_erase_error = false;

	if (program_only) {
		is_program_error = CAD_QSPI_STIG_FLAGSR_PROGRAMERROR(flag_sr);
	} else {
		is_erase_error = CAD_QSPI_STIG_FLAGSR_ERASEERROR(flag_sr);
	}

	if (is_program_error || is_erase_error) {
		cad_qspi_stig_cmd(cad_params, CAD_QSPI_STIG_OPCODE_CLFSR, 0);
	}
#else
	if (count >= CAD_QSPI_COMMAND_TIMEOUT) {
		LOG_ERR("Timed out waiting for program and erase\n");
	}

	if ((program_only && CAD_QSPI_STIG_FLAGSR_PROGRAMERROR(flag_sr)) ||
	    (!program_only && CAD_QSPI_STIG_FLAGSR_ERASEERROR(flag_sr))) {
		LOG_ERR("Error programming/erasing flash\n");
		cad_qspi_stig_cmd(cad_params, CAD_QSPI_STIG_OPCODE_CLFSR, 0);
		return -1;
	}
#endif

	return 0;
}
#endif

int cad_qspi_indirect_read_start_bank(struct cad_qspi_params *cad_params, uint32_t flash_addr,
				      uint32_t num_bytes)
{
	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	sys_write32(flash_addr, cad_params->reg_base + CAD_QSPI_INDRDSTADDR);
	sys_write32(num_bytes, cad_params->reg_base + CAD_QSPI_INDRDCNT);
	sys_write32(CAD_QSPI_INDRD_START | CAD_QSPI_INDRD_IND_OPS_DONE,
		    cad_params->reg_base + CAD_QSPI_INDRD);

	return 0;
}

int cad_qspi_indirect_write_start_bank(struct cad_qspi_params *cad_params, uint32_t flash_addr,
				       uint32_t num_bytes)
{
	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	sys_write32(flash_addr, cad_params->reg_base + CAD_QSPI_INDWRSTADDR);
	sys_write32(num_bytes, cad_params->reg_base + CAD_QSPI_INDWRCNT);
	sys_write32(CAD_QSPI_INDWR_START | CAD_QSPI_INDWR_INDDONE,
		    cad_params->reg_base + CAD_QSPI_INDWR);

	return 0;
}

int cad_qspi_indirect_write_finish(struct cad_qspi_params *cad_params)
{

	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

#if CAD_QSPI_MICRON_N25Q_SUPPORT
	return cad_qspi_n25q_wait_for_program_and_erase(cad_params, 1);
#else
	return 0;
#endif
}

int cad_qspi_enable(struct cad_qspi_params *cad_params)
{
	int status;

	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	sys_set_bits(cad_params->reg_base + CAD_QSPI_CFG, CAD_QSPI_CFG_ENABLE);

#if CAD_QSPI_MICRON_N25Q_SUPPORT
	status = cad_qspi_n25q_enable(cad_params);
	if (status != 0) {
		return status;
	}
#endif
	return 0;
}

int cad_qspi_enable_subsector_bank(struct cad_qspi_params *cad_params, uint32_t addr)
{
	int status = 0;

	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	status = cad_qspi_stig_cmd(cad_params, CAD_QSPI_STIG_OPCODE_WREN, 0);

	if (status != 0) {
		return status;
	}

	status = cad_qspi_stig_addr_cmd(cad_params, CAD_QSPI_STIG_OPCODE_SUBSEC_ERASE, 0, addr);
	if (status != 0) {
		return status;
	}

#if CAD_QSPI_MICRON_N25Q_SUPPORT
	status = cad_qspi_n25q_wait_for_program_and_erase(cad_params, 0);
#endif
	return status;
}

int cad_qspi_erase_subsector(struct cad_qspi_params *cad_params, uint32_t addr)
{
	int status = 0;

	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	status = cad_qspi_device_bank_select(cad_params, addr >> 24);
	if (status != 0) {
		return status;
	}

	return cad_qspi_enable_subsector_bank(cad_params, addr);
}

int cad_qspi_erase_sector(struct cad_qspi_params *cad_params, uint32_t addr)
{
	int status = 0;

	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	status = cad_qspi_device_bank_select(cad_params, addr >> 24);

	if (status != 0) {
		return status;
	}

	status = cad_qspi_stig_cmd(cad_params, CAD_QSPI_STIG_OPCODE_WREN, 0);

	if (status != 0) {
		return status;
	}

	status = cad_qspi_stig_addr_cmd(cad_params, CAD_QSPI_STIG_OPCODE_SEC_ERASE, 0, addr);
	if (status != 0) {
		return status;
	}

#if CAD_QSPI_MICRON_N25Q_SUPPORT
	status = cad_qspi_n25q_wait_for_program_and_erase(cad_params, 0);
#endif
	return status;
}

void cad_qspi_calibration(struct cad_qspi_params *cad_params, uint32_t dev_clk,
			  uint32_t qspi_clk_mhz)
{
	int status;
	uint32_t dev_sclk_mhz = 27; /*min value to get biggest 0xF div factor*/
	uint32_t data_cap_delay;
	uint32_t sample_rdid;
	uint32_t rdid;
	uint32_t div_actual;
	uint32_t div_bits;
	int first_pass, last_pass;

	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
	}

	/*1.  Set divider to bigger value (slowest SCLK)
	 *2.  RDID and save the value
	 */
	div_actual = (qspi_clk_mhz + (dev_sclk_mhz - 1)) / dev_sclk_mhz;
	div_bits = (((div_actual + 1) / 2) - 1);
	status = cad_qspi_set_baudrate_div(cad_params, 0xf);

	status = cad_qspi_stig_read_cmd(cad_params, CAD_QSPI_STIG_OPCODE_RDID, 0, 3, &sample_rdid);
	if (status != 0) {
		return;
	}

	/*3. Set divider to the intended frequency
	 *4.  Set the read delay = 0
	 *5.  RDID and check whether the value is same as item 2
	 *6.  Increase read delay and compared the value against item 2
	 *7.  Find the range of read delay that have same as
	 *    item 2 and divide it to 2
	 */
	div_actual = (qspi_clk_mhz + (dev_clk - 1)) / dev_clk;
	div_bits = (((div_actual + 1) / 2) - 1);
	status = cad_qspi_set_baudrate_div(cad_params, div_bits);

	if (status != 0) {
		return;
	}

	data_cap_delay = 0;
	first_pass = -1;
	last_pass = -1;

	do {
		if (status != 0) {
			break;
		}

		status = cad_qspi_stig_read_cmd(cad_params, CAD_QSPI_STIG_OPCODE_RDID, 0, 3, &rdid);
		if (status != 0) {
			break;
		}

		if (rdid == sample_rdid) {
			if (first_pass == -1) {
				first_pass = data_cap_delay;
			} else {
				last_pass = data_cap_delay;
			}
		}

		data_cap_delay++;

		sys_write32(CAD_QSPI_RDDATACAP_BYP(1) | CAD_QSPI_RDDATACAP_DELAY(data_cap_delay),
			    cad_params->reg_base + CAD_QSPI_RDDATACAP);

	} while (data_cap_delay < 0x10);

	if (first_pass > 0) {
		int diff = first_pass - last_pass;

		data_cap_delay = first_pass + diff / 2;
	}

	sys_write32(CAD_QSPI_RDDATACAP_BYP(1) | CAD_QSPI_RDDATACAP_DELAY(data_cap_delay),
		    cad_params->reg_base + CAD_QSPI_RDDATACAP);
	status = cad_qspi_stig_read_cmd(cad_params, CAD_QSPI_STIG_OPCODE_RDID, 0, 3, &rdid);

	if (status != 0) {
		return;
	}
}

int cad_qspi_int_disable(struct cad_qspi_params *cad_params, uint32_t mask)
{
	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	if (cad_qspi_idle(cad_params) == 0) {
		return -1;
	}

	if ((CAD_QSPI_INT_STATUS_ALL & mask) == 0) {
		return -1;
	}

	sys_write32(mask, cad_params->reg_base + CAD_QSPI_IRQMSK);
	return 0;
}

void cad_qspi_set_chip_select(struct cad_qspi_params *cad_params, int cs)
{

	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
	}

	cad_params->cad_qspi_cs = cs;
}

int cad_qspi_init(struct cad_qspi_params *cad_params, uint32_t clk_phase, uint32_t clk_pol,
		  uint32_t csda, uint32_t csdads, uint32_t cseot, uint32_t cssot,
		  uint32_t rddatacap)
{
	int status = 0;
	uint32_t qspi_desired_clk_freq;
	uint32_t rdid = 0;
	uint32_t cap_code;

	LOG_INF("Initializing Qspi");
	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter");
		return -EINVAL;
	}

	if (cad_qspi_idle(cad_params) == 0) {
		LOG_ERR("device not idle");
		return -EBUSY;
	}
	status = cad_qspi_timing_config(cad_params, clk_phase, clk_pol, csda, csdads, cseot, cssot,
					rddatacap);

	if (status != 0) {
		LOG_ERR("config set timing failure\n");
		return status;
	}

	sys_write32(CAD_QSPI_REMAPADDR_VALUE_SET(0), cad_params->reg_base + CAD_QSPI_REMAPADDR);

	status = cad_qspi_int_disable(cad_params, CAD_QSPI_INT_STATUS_ALL);
	if (status != 0) {
		LOG_ERR("failed disable\n");
		return status;
	}

	cad_qspi_set_baudrate_div(cad_params, 0xf);
	status = cad_qspi_enable(cad_params);
	if (status != 0) {
		LOG_ERR("failed enable\n");
		return status;
	}

	qspi_desired_clk_freq = 100;
	cad_qspi_calibration(cad_params, qspi_desired_clk_freq, cad_params->clk_rate);

	status = cad_qspi_stig_read_cmd(cad_params, CAD_QSPI_STIG_OPCODE_RDID, 0, 3, &rdid);

	if (status != 0) {
		LOG_ERR("Error reading RDID\n");
		return status;
	}

	/*
	 * NOTE: The Size code seems to be a form of BCD (binary coded decimal).
	 * The first nibble is the 10's digit and the second nibble is the 1's
	 * digit in the number of bytes.
	 *
	 * Capacity ID samples:
	 * 0x15 :   16 Mb =>   2 MiB => 1 << 21 ; BCD=15
	 * 0x16 :   32 Mb =>   4 MiB => 1 << 22 ; BCD=16
	 * 0x17 :   64 Mb =>   8 MiB => 1 << 23 ; BCD=17
	 * 0x18 :  128 Mb =>  16 MiB => 1 << 24 ; BCD=18
	 * 0x19 :  256 Mb =>  32 MiB => 1 << 25 ; BCD=19
	 * 0x1a
	 * 0x1b
	 * 0x1c
	 * 0x1d
	 * 0x1e
	 * 0x1f
	 * 0x20 :  512 Mb =>  64 MiB => 1 << 26 ; BCD=20
	 * 0x21 : 1024 Mb => 128 MiB => 1 << 27 ; BCD=21
	 */

	cap_code = CAD_QSPI_STIG_RDID_CAPACITYID(rdid);

	if (!(((cap_code >> 4) > 0x9) || ((cap_code & 0xf) > 0x9))) {
		uint32_t decoded_cap = ((cap_code >> 4) * 10) + (cap_code & 0xf);

		cad_params->qspi_device_size = 1 << (decoded_cap + 6);
		LOG_INF("QSPI Capacity: %x", cad_params->qspi_device_size);

	} else {
		LOG_ERR("Invalid CapacityID encountered: 0x%02x", cap_code);
		return -1;
	}

	cad_qspi_configure_dev_size(cad_params, QSPI_ADDR_BYTES, QSPI_BYTES_PER_DEV,
				    QSPI_BYTES_PER_BLOCK);

	LOG_INF("Flash size: %d Bytes", cad_params->qspi_device_size);

	return status;
}

int cad_qspi_indirect_page_bound_write(struct cad_qspi_params *cad_params, uint32_t offset,
				       uint8_t *buffer, uint32_t len)
{
	int status = 0, i;
	uint32_t write_count, write_capacity, *write_data, space, write_fill_level, sram_partition;

	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	status = cad_qspi_indirect_write_start_bank(cad_params, offset, len);

	if (status != 0) {
		return status;
	}

	write_count = 0;
	sram_partition =
		CAD_QSPI_SRAMPART_ADDR(sys_read32(cad_params->reg_base + CAD_QSPI_SRAMPART));
	write_capacity = (cad_params->sram_fifo_size / sizeof(uint32_t)) - sram_partition;

	while (write_count < len) {
		write_fill_level = CAD_QSPI_SRAMFILL_INDWRPART(
			sys_read32(cad_params->reg_base + CAD_QSPI_SRAMFILL));
		space = MIN(write_capacity - write_fill_level,
			    (len - write_count) / sizeof(uint32_t));
		write_data = (uint32_t *)(buffer + write_count);
		for (i = 0; i < space; ++i) {
			sys_write32(*write_data++, cad_params->data_base);
		}

		write_count += space * sizeof(uint32_t);
	}
	return cad_qspi_indirect_write_finish(cad_params);
}

int cad_qspi_read_bank(struct cad_qspi_params *cad_params, uint8_t *buffer, uint32_t offset,
		       uint32_t size)
{
	int status;
	uint32_t read_count = 0, *read_data;
	int level = 1, count = 0, i;

	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	status = cad_qspi_indirect_read_start_bank(cad_params, offset, size);

	if (status != 0) {
		return status;
	}

	while (read_count < size) {
		do {
			level = CAD_QSPI_SRAMFILL_INDRDPART(
				sys_read32(cad_params->reg_base + CAD_QSPI_SRAMFILL));
			read_data = (uint32_t *)(buffer + read_count);
			for (i = 0; i < level; ++i) {
				*read_data++ = sys_read32(cad_params->data_base);
			}

			read_count += level * sizeof(uint32_t);
			count++;
		} while (level > 0);
	}

	return 0;
}

int cad_qspi_write_bank(struct cad_qspi_params *cad_params, uint32_t offset, uint8_t *buffer,
			uint32_t size)
{
	int status = 0;
	uint32_t page_offset = offset & (CAD_QSPI_PAGE_SIZE - 1);
	uint32_t write_size = MIN(size, CAD_QSPI_PAGE_SIZE - page_offset);

	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	while (size) {
		status = cad_qspi_indirect_page_bound_write(cad_params, offset, buffer, write_size);
		if (status != 0) {
			break;
		}

		offset += write_size;
		buffer += write_size;
		size -= write_size;
		write_size = MIN(size, CAD_QSPI_PAGE_SIZE);
	}
	return status;
}

int cad_qspi_read(struct cad_qspi_params *cad_params, void *buffer, uint32_t offset, uint32_t size)
{
	uint32_t bank_count, bank_addr, bank_offset, copy_len;
	uint8_t *read_data;
	int i, status;

	status = 0;

	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	if ((offset >= cad_params->qspi_device_size) ||
	    (offset + size - 1 >= cad_params->qspi_device_size) || (size == 0)) {
		LOG_ERR("Invalid read parameter\n");
		return -EINVAL;
	}

	if (CAD_QSPI_INDRD_RD_STAT(sys_read32(cad_params->reg_base + CAD_QSPI_INDRD))) {
		LOG_ERR("Read in progress\n");
		return -ENOTBLK;
	}

	/*
	 * bank_count : Number of bank(s) affected, including partial banks.
	 * bank_addr  : Aligned address of the first bank,
	 *		including partial bank.
	 * bank_ofst  : The offset of the bank to read.
	 *		Only used when reading the first bank.
	 */
	bank_count = CAD_QSPI_BANK_ADDR(offset + size - 1) - CAD_QSPI_BANK_ADDR(offset) + 1;
	bank_addr = offset & CAD_QSPI_BANK_ADDR_MSK;
	bank_offset = offset & (CAD_QSPI_BANK_SIZE - 1);

	read_data = (uint8_t *)buffer;

	copy_len = MIN(size, CAD_QSPI_BANK_SIZE - bank_offset);

	for (i = 0; i < bank_count; ++i) {
		status = cad_qspi_device_bank_select(cad_params, CAD_QSPI_BANK_ADDR(bank_addr));
		if (status != 0) {
			break;
		}

		status = cad_qspi_read_bank(cad_params, read_data, bank_offset, copy_len);

		if (status != 0) {
			break;
		}

		bank_addr += CAD_QSPI_BANK_SIZE;
		read_data += copy_len;
		size -= copy_len;
		bank_offset = 0;
		copy_len = MIN(size, CAD_QSPI_BANK_SIZE);
	}

	return status;
}

int cad_qspi_erase(struct cad_qspi_params *cad_params, uint32_t offset, uint32_t size)
{
	int status = 0;
	k_spinlock_key_t key;
	uint32_t subsector_offset = offset & (CAD_QSPI_SUBSECTOR_SIZE - 1);
	uint32_t erase_size = MIN(size, CAD_QSPI_SUBSECTOR_SIZE - subsector_offset);

	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	while (size) {
		key = cad_qspi_critical_enter();
		status = cad_qspi_erase_subsector(cad_params, offset);
		cad_qspi_critical_exit(key);

		if (status != 0) {
			break;
		}

		offset += erase_size;
		size -= erase_size;
		erase_size = MIN(size, CAD_QSPI_SUBSECTOR_SIZE);
	}
	return status;
}

int cad_qspi_write(struct cad_qspi_params *cad_params, void *buffer, uint32_t offset, uint32_t size)
{
	int status, i;
	uint32_t bank_count, bank_addr, bank_offset, copy_len;
	uint8_t *write_data;

	status = 0;

	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	if ((offset >= cad_params->qspi_device_size) ||
	    (offset + size - 1 >= cad_params->qspi_device_size) || (size == 0)) {
		return -EINVAL;
	}

	if (CAD_QSPI_INDWR_RDSTAT(sys_read32(cad_params->reg_base + CAD_QSPI_INDWR))) {
		LOG_ERR("QSPI Error: Write in progress\n");
		return -ENOTBLK;
	}

	bank_count = CAD_QSPI_BANK_ADDR(offset + size - 1) - CAD_QSPI_BANK_ADDR(offset) + 1;
	bank_addr = offset & CAD_QSPI_BANK_ADDR_MSK;
	bank_offset = offset & (CAD_QSPI_BANK_SIZE - 1);

	write_data = buffer;

	copy_len = MIN(size, CAD_QSPI_BANK_SIZE - bank_offset);

	for (i = 0; i < bank_count; ++i) {
		status = cad_qspi_device_bank_select(cad_params, CAD_QSPI_BANK_ADDR(bank_addr));

		if (status != 0) {
			break;
		}

		status = cad_qspi_write_bank(cad_params, bank_offset, write_data, copy_len);
		if (status != 0) {
			break;
		}

		bank_addr += CAD_QSPI_BANK_SIZE;
		write_data += copy_len;
		size -= copy_len;
		bank_offset = 0;

		copy_len = MIN(size, CAD_QSPI_BANK_SIZE);
	}
	return status;
}

int cad_qspi_update(struct cad_qspi_params *cad_params, void *Buffer, uint32_t offset,
		    uint32_t size)
{
	int status = 0;

	if (cad_params == NULL) {
		LOG_ERR("Wrong parameter\n");
		return -EINVAL;
	}

	status = cad_qspi_erase(cad_params, offset, size);

	if (status != 0) {
		return status;
	}

	return cad_qspi_write(cad_params, Buffer, offset, size);
}

void cad_qspi_reset(struct cad_qspi_params *cad_params)
{
	cad_qspi_stig_cmd(cad_params, CAD_QSPI_STIG_OPCODE_RESET_EN, 0);
	cad_qspi_stig_cmd(cad_params, CAD_QSPI_STIG_OPCODE_RESET_MEM, 0);
}

#ifdef CONFIG_QSPI_DW_DMA
/* Wrapper function for DMA compatibility */
static int cad_qspi_indirect_write_start(struct cad_qspi_params *cad_params, uint32_t flash_addr,
					 uint32_t num_bytes)
{
	return cad_qspi_indirect_write_start_bank(cad_params, flash_addr, num_bytes);
}

/* DMA callback function */
static void cad_qspi_dma_callback(const struct device *dma_dev, void *user_data, uint32_t channel,
				  int status)
{
	struct cad_qspi_params *cad_params = (struct cad_qspi_params *)user_data;

	LOG_DBG("DMA callback from %s: channel %d, status %d", dma_dev->name, channel, status);

	if (status != DMA_STATUS_COMPLETE) {
		LOG_ERR("DMA transfer failed with status %d", status);
		cad_params->dma_stat |= CAD_QSPI_DMA_ERROR_FLAG;
	} else {
		if (channel == cad_params->dma_tx_channel) {
			cad_params->dma_stat |= CAD_QSPI_DMA_TX_DONE_FLAG;
			LOG_DBG("DMA TX channel done");
		} else if (channel == cad_params->dma_rx_channel) {
			cad_params->dma_stat |= CAD_QSPI_DMA_RX_DONE_FLAG;
			LOG_DBG("DMA RX channel done");
		}
	}

	/* When both TX and RX are done or error occurred, release semaphore */
	if (((cad_params->dma_stat & CAD_QSPI_DMA_DONE_FLAG) == CAD_QSPI_DMA_DONE_FLAG) ||
	    (cad_params->dma_stat & CAD_QSPI_DMA_ERROR_FLAG)) {
		LOG_DBG("DMA transfer completed, releasing semaphore");
		k_sem_give(&cad_params->dma_sem);
	}
}

/* Initialize DMA support */
int cad_qspi_dma_init(struct cad_qspi_params *cad_params)
{
	if (!cad_params) {
		return -EINVAL;
	}

	/* Initialize DMA disabled flag */
	cad_params->dma_disabled = false;

	/* Initialize DMA semaphore */
	k_sem_init(&cad_params->dma_sem, 0, 1);

	/* Check if DMA devices are ready */
	if (cad_params->dma_tx_dev && !device_is_ready(cad_params->dma_tx_dev)) {
		LOG_ERR("TX DMA device not ready");
		return -ENODEV;
	}

	if (cad_params->dma_rx_dev && !device_is_ready(cad_params->dma_rx_dev)) {
		LOG_ERR("RX DMA device not ready");
		return -ENODEV;
	}

	/* Configure TX DMA channel */
	if (cad_params->dma_tx_dev) {
		cad_params->tx_dma_cfg.channel_direction = MEMORY_TO_PERIPHERAL;
		cad_params->tx_dma_cfg.dma_callback = cad_qspi_dma_callback;
		cad_params->tx_dma_cfg.user_data = cad_params;
		cad_params->tx_dma_cfg.source_burst_length = 1; /* Single transfers */
		cad_params->tx_dma_cfg.dest_burst_length = 1;
		cad_params->tx_dma_cfg.source_data_size = 4; /* 32-bit transfers */
		cad_params->tx_dma_cfg.dest_data_size = 4;
		cad_params->tx_dma_cfg.block_count = 1;
		cad_params->tx_dma_cfg.dma_slot = cad_params->dma_tx_slot;
		cad_params->tx_dma_cfg.source_handshake = 0; /* Memory side no handshake */
		cad_params->tx_dma_cfg.dest_handshake = 0;   /* Peripheral side with handshake */
	}

	/* Configure RX DMA channel */
	if (cad_params->dma_rx_dev) {
		cad_params->rx_dma_cfg.channel_direction = PERIPHERAL_TO_MEMORY;
		cad_params->rx_dma_cfg.dma_callback = cad_qspi_dma_callback;
		cad_params->rx_dma_cfg.user_data = cad_params;
		cad_params->rx_dma_cfg.source_burst_length = 1; /* Single transfers */
		cad_params->rx_dma_cfg.dest_burst_length = 1;
		cad_params->rx_dma_cfg.source_data_size = 4; /* 32-bit transfers */
		cad_params->rx_dma_cfg.dest_data_size = 4;
		cad_params->rx_dma_cfg.block_count = 1;
		cad_params->rx_dma_cfg.dma_slot = cad_params->dma_rx_slot;
		cad_params->rx_dma_cfg.source_handshake = 0; /* Peripheral side with handshake */
		cad_params->rx_dma_cfg.dest_handshake = 0;   /* Memory side no handshake */
	}

	return 0;
}

/* Debug function to print QSPI register status */
static void cad_qspi_debug_regs(struct cad_qspi_params *cad_params, const char *context)
{
	uint32_t indrd_reg = sys_read32(cad_params->reg_base + CAD_QSPI_INDRD);
	uint32_t indwr_reg = sys_read32(cad_params->reg_base + CAD_QSPI_INDWR);
	uint32_t cfg_reg = sys_read32(cad_params->reg_base + CAD_QSPI_CFG);

	LOG_DBG("%s: INDRD=0x%x (RD_STAT=%d,DONE=%d), INDWR=0x%x (WR_STAT=%d,DONE=%d), CFG=0x%x, "
		"IDLE=%s",
		context, indrd_reg, CAD_QSPI_INDRD_RD_STAT(indrd_reg),
		(indrd_reg & CAD_QSPI_INDRD_IND_OPS_DONE) ? 1 : 0, indwr_reg,
		CAD_QSPI_INDWR_RDSTAT(indwr_reg), (indwr_reg & CAD_QSPI_INDWR_INDDONE) ? 1 : 0,
		cfg_reg, cad_qspi_idle(cad_params) ? "YES" : "NO");
}

/* Enable DMA mode for QSPI controller */
static void cad_qspi_enable_dma_mode(struct cad_qspi_params *cad_params)
{
	uint32_t reg_val;

	/* Debug: Check initial state */
	cad_qspi_debug_regs(cad_params, "Before DMA enable");

	/* Enable DMA mode in QSPI configuration register (bit 15) */
	reg_val = sys_read32(cad_params->reg_base + CAD_QSPI_CFG);
	reg_val |= CAD_QSPI_CFG_ENDMA;
	sys_write32(reg_val, cad_params->reg_base + CAD_QSPI_CFG);

	/* Set DMA peripheral configuration - single transfers */
	sys_write32(0x00000000, cad_params->reg_base + CAD_QSPI_DMA_CFG);

	/* Set watermark levels for DMA triggers - adjusted for single transfers */
	sys_write32(0x00000001,
		    cad_params->reg_base + CAD_QSPI_INDRDWATERMARK); /* RX watermark = 1 word */
	sys_write32(0x00000001,
		    cad_params->reg_base + CAD_QSPI_INDWRWATERMARK); /* TX watermark = 1 word */

	LOG_DBG("DMA mode enabled in QSPI controller with watermarks");
}

/* Disable DMA mode for QSPI controller */
static void cad_qspi_disable_dma_mode(struct cad_qspi_params *cad_params)
{
	uint32_t reg_val;

	/* Disable DMA mode in QSPI configuration register */
	reg_val = sys_read32(cad_params->reg_base + CAD_QSPI_CFG);
	reg_val &= ~CAD_QSPI_CFG_ENDMA;
	sys_write32(reg_val, cad_params->reg_base + CAD_QSPI_CFG);

	LOG_DBG("DMA mode disabled in QSPI controller");
}

/* Configure DMA for read operation */
static int cad_qspi_configure_dma_read(struct cad_qspi_params *cad_params, void *buffer,
				       uint32_t size)
{
	uintptr_t qspi_phys_addr, buffer_phys_addr;
	int ret;

	/* Get QSPI data FIFO register physical address
	 * For QSPI DMA, we need to use the actual data FIFO register, not the memory-mapped region
	 * The QSPI data FIFO is typically at offset 0x0 in some controllers, but let's use
	 * the memory-mapped data base since that's what the hardware is configured for
	 */
	qspi_phys_addr = cad_params->data_base;

	/* Validate QSPI data base address */
	if (qspi_phys_addr == 0) {
		LOG_ERR("Invalid QSPI data base address");
		return -EINVAL;
	}

	/* For debugging: print the addresses being used */
	LOG_DBG("QSPI reg_base=0x%lx, data_base=0x%lx", cad_params->reg_base,
		cad_params->data_base);

	/* Get buffer physical address */
#ifdef CONFIG_MMU
	buffer_phys_addr = k_mem_phys_addr(buffer);
#else
	buffer_phys_addr = (uintptr_t)buffer;
#endif

	/* Ensure size is 32-bit aligned for DMA */
	if (size & 0x3) {
		LOG_ERR("DMA size must be 4-byte aligned, got %d", size);
		return -EINVAL;
	}

	/* Invalidate cache before DMA operation */
#ifdef CONFIG_DCACHE
	sys_cache_data_invd_range(buffer, size);
#endif

	/* Configure RX DMA block parameters */
	cad_params->rx_blk_cfg.source_address = (uint32_t)qspi_phys_addr;
	cad_params->rx_blk_cfg.dest_address = (uint32_t)buffer_phys_addr;
	cad_params->rx_blk_cfg.block_size = size / 4; /* Transfer size in 32-bit words */
	cad_params->rx_blk_cfg.source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
	cad_params->rx_blk_cfg.dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;

	cad_params->rx_dma_cfg.head_block = &cad_params->rx_blk_cfg;

	ret = dma_config(cad_params->dma_rx_dev, cad_params->dma_rx_channel,
			 &cad_params->rx_dma_cfg);
	if (ret) {
		LOG_ERR("Failed to configure RX DMA: %d", ret);
		return ret;
	}

	LOG_DBG("RX DMA configured: src=0x%x, dst=0x%x, words=%d",
		cad_params->rx_blk_cfg.source_address, cad_params->rx_blk_cfg.dest_address,
		cad_params->rx_blk_cfg.block_size);

	return 0;
}

/* Configure DMA for write operation */
static int cad_qspi_configure_dma_write(struct cad_qspi_params *cad_params, void *buffer,
					uint32_t size)
{
	uintptr_t qspi_phys_addr, buffer_phys_addr;
	int ret;

	/* Get QSPI data FIFO register physical address
	 * For QSPI DMA, we need to use the actual data FIFO register, not the memory-mapped region
	 */
	qspi_phys_addr = cad_params->data_base;

	/* Validate QSPI data base address */
	if (qspi_phys_addr == 0) {
		LOG_ERR("Invalid QSPI data base address");
		return -EINVAL;
	}

	/* For debugging: print the addresses being used */
	LOG_DBG("QSPI reg_base=0x%lx, data_base=0x%lx", cad_params->reg_base,
		cad_params->data_base);

	/* Get buffer physical address */
#ifdef CONFIG_MMU
	buffer_phys_addr = k_mem_phys_addr(buffer);
#else
	buffer_phys_addr = (uintptr_t)buffer;
#endif

	/* Ensure size is 32-bit aligned for DMA */
	if (size & 0x3) {
		LOG_ERR("DMA size must be 4-byte aligned, got %d", size);
		return -EINVAL;
	}

	/* Flush cache before DMA operation */
#ifdef CONFIG_DCACHE
	sys_cache_data_flush_range(buffer, size);
#endif

	/* Debug: Print first few bytes of write buffer */
	uint8_t *debug_buf = (uint8_t *)buffer;

	LOG_DBG("TX buffer first 32 bytes:");
	for (int i = 0; i < 32 && i < size; i += 4) {
		LOG_DBG("  [%02d]: 0x%02x 0x%02x 0x%02x 0x%02x", i, debug_buf[i], debug_buf[i + 1],
			debug_buf[i + 2], debug_buf[i + 3]);
	}

	/* Configure TX DMA block parameters */
	cad_params->tx_blk_cfg.source_address = (uint32_t)buffer_phys_addr;
	cad_params->tx_blk_cfg.dest_address = (uint32_t)qspi_phys_addr;
	cad_params->tx_blk_cfg.block_size = size / 4; /* Transfer size in 32-bit words */
	cad_params->tx_blk_cfg.source_addr_adj = DMA_ADDR_ADJ_INCREMENT;
	cad_params->tx_blk_cfg.dest_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;

	cad_params->tx_dma_cfg.head_block = &cad_params->tx_blk_cfg;

	ret = dma_config(cad_params->dma_tx_dev, cad_params->dma_tx_channel,
			 &cad_params->tx_dma_cfg);
	if (ret) {
		LOG_ERR("Failed to configure TX DMA: %d", ret);
		return ret;
	}

	LOG_DBG("TX DMA configured: src=0x%x, dst=0x%x, words=%d",
		cad_params->tx_blk_cfg.source_address, cad_params->tx_blk_cfg.dest_address,
		cad_params->tx_blk_cfg.block_size);

	return 0;
}

/* Start DMA transfer */
static int cad_qspi_start_dma(struct cad_qspi_params *cad_params, bool is_read)
{
	int ret;

	cad_params->dma_stat = 0;

	if (is_read) {
		/* For read operations, start RX DMA */
		ret = dma_start(cad_params->dma_rx_dev, cad_params->dma_rx_channel);
		if (ret) {
			LOG_ERR("Failed to start RX DMA: %d", ret);
			return ret;
		}
		/* Mark TX as done since we're only doing RX */
		cad_params->dma_stat |= CAD_QSPI_DMA_TX_DONE_FLAG;
		LOG_DBG("RX DMA started successfully");
	} else {
		/* For write operations, start TX DMA */
		ret = dma_start(cad_params->dma_tx_dev, cad_params->dma_tx_channel);
		if (ret) {
			LOG_ERR("Failed to start TX DMA: %d", ret);
			return ret;
		}
		/* Mark RX as done since we're only doing TX */
		cad_params->dma_stat |= CAD_QSPI_DMA_RX_DONE_FLAG;
		LOG_DBG("TX DMA started successfully");
	}

	return 0;
}

/* Wait for DMA transfer to complete */
static int cad_qspi_wait_dma_complete(struct cad_qspi_params *cad_params)
{
	int ret;

	ret = k_sem_take(&cad_params->dma_sem, K_MSEC(CAD_QSPI_DMA_TIMEOUT_MS));
	if (ret) {
		LOG_DBG("DMA transfer timeout");
		return -ETIMEDOUT;
	}

	/* Check for DMA errors */
	if (cad_params->dma_stat & CAD_QSPI_DMA_ERROR_FLAG) {
		LOG_ERR("DMA transfer error");
		return -EIO;
	}

	LOG_DBG("DMA transfer completed successfully");
	return 0;
}

/* Stop DMA channels */
static void cad_qspi_stop_dma(struct cad_qspi_params *cad_params, bool is_read)
{
	if (is_read && cad_params->dma_rx_dev) {
		dma_stop(cad_params->dma_rx_dev, cad_params->dma_rx_channel);
	}
	if (!is_read && cad_params->dma_tx_dev) {
		dma_stop(cad_params->dma_tx_dev, cad_params->dma_tx_channel);
	}
}

/* Check if DMA should be used for the transfer */
static bool cad_qspi_should_use_dma(struct cad_qspi_params *cad_params, uint32_t size)
{
	/* Don't use DMA if it has been disabled due to previous failures */
	if (cad_params->dma_disabled) {
		LOG_DBG("DMA disabled due to previous failures, using polling");
		return false;
	}

	/* Don't use DMA if devices are not available */
	if (!cad_params->dma_tx_dev || !cad_params->dma_rx_dev) {
		LOG_DBG("DMA devices not available, using polling");
		return false;
	}

	/* Use DMA for larger transfers (threshold: CAD_QSPI_DMA_TEST_THRESHOLD bytes) */
	if (size >= CAD_QSPI_DMA_TEST_THRESHOLD) {
		LOG_DBG("Using DMA for %d byte transfer", size);
		return true;
	}

	LOG_DBG("Using polling for small %d byte transfer", size);
	return false;
}

/* DMA-based read implementation for single bank */
static int cad_qspi_dma_read_bank(struct cad_qspi_params *cad_params, uint8_t *buffer,
				  uint32_t offset, uint32_t size)
{
	int ret;

	LOG_DBG("DMA read bank: offset=0x%x, size=%d", offset, size);

	/* Pre-invalidate cache to ensure fresh read data */
#ifdef CONFIG_DCACHE
	sys_cache_data_invd_range(buffer, size);
#endif

	/* Configure DMA for read */
	ret = cad_qspi_configure_dma_read(cad_params, buffer, size);
	if (ret) {
		LOG_ERR("Failed to configure DMA read: %d", ret);
		return ret;
	}

	/* Enable DMA mode */
	cad_qspi_enable_dma_mode(cad_params);

	/* Start DMA transfer */
	ret = cad_qspi_start_dma(cad_params, true);
	if (ret) {
		LOG_ERR("Failed to start DMA: %d", ret);
		goto cleanup;
	}

	/* Start indirect read operation */
	ret = cad_qspi_indirect_read_start_bank(cad_params, offset, size);
	if (ret) {
		LOG_ERR("Failed to start indirect read: %d", ret);
		cad_qspi_stop_dma(cad_params, true);
		goto cleanup;
	}

	/* Wait for DMA completion */
	ret = cad_qspi_wait_dma_complete(cad_params);
	if (ret) {
		LOG_ERR("DMA read timeout: %d", ret);
		cad_qspi_stop_dma(cad_params, true);
		goto cleanup;
	}

	/* Simple status clearing */
	sys_write32(CAD_QSPI_INDRD_IND_OPS_DONE, cad_params->reg_base + CAD_QSPI_INDRD);

	/* Invalidate cache after DMA completion */
#ifdef CONFIG_DCACHE
	sys_cache_data_invd_range(buffer, size);
#endif

	LOG_DBG("DMA read completed successfully");
	ret = 0;

cleanup:
	cad_qspi_disable_dma_mode(cad_params);
	return ret;
}

/* DMA-based write implementation for single bank */
static int cad_qspi_dma_write_bank(struct cad_qspi_params *cad_params, uint32_t offset,
				   uint8_t *buffer, uint32_t size)
{
	int ret;

	LOG_DBG("DMA write bank: offset=0x%x, size=%d", offset, size);

	/* Configure DMA for write */
	ret = cad_qspi_configure_dma_write(cad_params, buffer, size);
	if (ret) {
		LOG_ERR("Failed to configure DMA write: %d", ret);
		return ret;
	}

	/* Enable DMA mode */
	cad_qspi_enable_dma_mode(cad_params);

	/* Start DMA transfer */
	ret = cad_qspi_start_dma(cad_params, false);
	if (ret) {
		LOG_ERR("Failed to start DMA: %d", ret);
		goto cleanup;
	}

	/* Start indirect write operation */
	ret = cad_qspi_indirect_write_start(cad_params, offset, size);
	if (ret) {
		LOG_ERR("Failed to start indirect write: %d", ret);
		cad_qspi_stop_dma(cad_params, false);
		goto cleanup;
	}

	/* Wait for DMA completion */
	ret = cad_qspi_wait_dma_complete(cad_params);
	if (ret) {
		LOG_DBG("DMA write timeout: %d", ret);
		cad_qspi_stop_dma(cad_params, false);
		goto cleanup;
	}

	/* Simple status clearing */
	sys_write32(CAD_QSPI_INDWR_INDDONE, cad_params->reg_base + CAD_QSPI_INDWR);

	/* Finish indirect write */
	cad_qspi_indirect_write_finish(cad_params);
	LOG_DBG("DMA write completed successfully");
	ret = 0;

cleanup:
	cad_qspi_disable_dma_mode(cad_params);
	return ret;
}

int cad_qspi_dma_read(struct cad_qspi_params *cad_params, void *buffer, uint32_t offset,
		      uint32_t size)
{
	uint32_t bank_count, bank_addr, bank_offset, copy_len;
	uint8_t *read_data;
	int i, status;
	bool use_dma;

	if (!cad_params || !buffer) {
		LOG_ERR("Invalid parameters");
		return -EINVAL;
	}

	if ((offset >= cad_params->qspi_device_size) ||
	    (offset + size - 1 >= cad_params->qspi_device_size) || (size == 0)) {
		LOG_ERR("Invalid read parameter");
		return -EINVAL;
	}

	/* Check if DMA should be used */
	use_dma = cad_qspi_should_use_dma(cad_params, size);

	LOG_DBG("Reading %d bytes at offset 0x%x using %s mode", size, offset,
		use_dma ? "DMA" : "polling");

	/* Calculate bank parameters for multi-bank operations */
	bank_count = CAD_QSPI_BANK_ADDR(offset + size - 1) - CAD_QSPI_BANK_ADDR(offset) + 1;
	bank_addr = offset & CAD_QSPI_BANK_ADDR_MSK;
	bank_offset = offset & (CAD_QSPI_BANK_SIZE - 1);

	read_data = (uint8_t *)buffer;
	copy_len = MIN(size, CAD_QSPI_BANK_SIZE - bank_offset);

	for (i = 0; i < bank_count; ++i) {
		status = cad_qspi_device_bank_select(cad_params, CAD_QSPI_BANK_ADDR(bank_addr));
		if (status != 0) {
			break;
		}

		if (use_dma) {
			/* Try DMA first */
			status = cad_qspi_dma_read_bank(cad_params, read_data, bank_offset,
							copy_len);

			if (status != 0) {
				LOG_DBG("DMA read failed, disabling DMA and falling back to "
					"polling");
				cad_params->dma_disabled = true;
				use_dma = false;

				status = cad_qspi_read_bank(cad_params, read_data, bank_offset,
							    copy_len);
			}
		} else {
			status = cad_qspi_read_bank(cad_params, read_data, bank_offset, copy_len);
		}

		if (status != 0) {
			break;
		}

		bank_addr += CAD_QSPI_BANK_SIZE;
		read_data += copy_len;
		size -= copy_len;
		bank_offset = 0;
		copy_len = MIN(size, CAD_QSPI_BANK_SIZE);
	}

	return status;
}

/* DMA-enhanced write function with fallback to polling */
int cad_qspi_dma_write(struct cad_qspi_params *cad_params, void *buffer, uint32_t offset,
		       uint32_t size)
{
	uint32_t bank_count, bank_addr, bank_offset, copy_len;
	uint8_t *write_data;
	int i, status;
	bool use_dma;

	if (!cad_params || !buffer) {
		LOG_ERR("Invalid parameters");
		return -EINVAL;
	}

	if ((offset >= cad_params->qspi_device_size) ||
	    (offset + size - 1 >= cad_params->qspi_device_size) || (size == 0)) {
		LOG_ERR("Invalid write parameter");
		return -EINVAL;
	}

	/* Check if DMA should be used */
	use_dma = cad_qspi_should_use_dma(cad_params, size);

	LOG_DBG("Writing %d bytes at offset 0x%x using %s mode", size, offset,
		use_dma ? "DMA" : "polling");

	/* Calculate bank parameters for multi-bank operations */
	bank_count = CAD_QSPI_BANK_ADDR(offset + size - 1) - CAD_QSPI_BANK_ADDR(offset) + 1;
	bank_addr = offset & CAD_QSPI_BANK_ADDR_MSK;
	bank_offset = offset & (CAD_QSPI_BANK_SIZE - 1);

	write_data = (uint8_t *)buffer;
	copy_len = MIN(size, CAD_QSPI_BANK_SIZE - bank_offset);

	for (i = 0; i < bank_count; ++i) {
		status = cad_qspi_device_bank_select(cad_params, CAD_QSPI_BANK_ADDR(bank_addr));
		if (status != 0) {
			break;
		}

		if (use_dma) {
			/* Try DMA first */
			status = cad_qspi_dma_write_bank(cad_params, bank_offset, write_data,
							 copy_len);

			if (status != 0) {
				LOG_DBG("DMA write failed, disabling DMA and falling back to "
					"polling");
				cad_params->dma_disabled = true;
				use_dma = false;
			} else {
				LOG_DBG("DMA write completed successfully");
				continue;
			}
		}
		if (copy_len >= CAD_QSPI_PAGE_SIZE) {
			uint32_t last_256_offset = bank_offset + copy_len - CAD_QSPI_PAGE_SIZE;
			uint8_t *last_256_data = write_data + copy_len - CAD_QSPI_PAGE_SIZE;

			LOG_DBG("Writing %d bytes, will rewrite last CAD_QSPI_PAGE_SIZE bytes",
				copy_len);

			status = cad_qspi_write_bank(cad_params, bank_offset, write_data, copy_len);

			if (status == 0) {
				LOG_DBG("Rewriting last CAD_QSPI_PAGE_SIZE bytes at offset 0x%x",
					last_256_offset);
				status = cad_qspi_write_bank(cad_params, last_256_offset,
							     last_256_data, CAD_QSPI_PAGE_SIZE);
			}
		} else {
			status = cad_qspi_write_bank(cad_params, bank_offset, write_data, copy_len);
		}

		if (status != 0) {
			break;
		}

		bank_addr += CAD_QSPI_BANK_SIZE;
		write_data += copy_len;
		size -= copy_len;
		bank_offset = 0;
		copy_len = MIN(size, CAD_QSPI_BANK_SIZE);
	}

	return status;
}

#endif /* CONFIG_QSPI_DW_DMA */
