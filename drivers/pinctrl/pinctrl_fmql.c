/**
 * Copyright (c) 2024 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/syscon.h>
#include <zephyr/dt-bindings/pinctrl/fmql-pinctrl.h>

#define GET_MIO(mux) (((mux) >> MIO_SHIFTER) & MIO_MASK)
#define GET_PIN(mux) (((mux) >> PIN_SHIFTER) & PIN_MASK)
#define GET_MUX(mux) ((mux) & PINMUX_MASK)

#define PINCTRL     DT_NODELABEL(pinctrl)
#define PINCTRL_DEV DEVICE_DT_GET(PINCTRL)
#define PINCTRL_REG DT_REG_ADDR(PINCTRL)

#define SYSCON     DT_NODELABEL(slcr)
#define SYSCON_DEV DEVICE_DT_GET(SYSCON)
#define SYSCON_REG (DT_REG_ADDR(SYSCON))

#define SLCR_LOCK       0x0004
#define SLCR_UNLOCK     0x0008
#define SLCR_UNLOCK_KEY 0xDF0D767B

static inline void fmql_syscon_unlock(void)
{
	syscon_write_reg(SYSCON_DEV, SLCR_UNLOCK, SLCR_UNLOCK_KEY);
}

static inline void fmql_syscon_lock(void)
{
	syscon_write_reg(SYSCON_DEV, SLCR_LOCK, SLCR_UNLOCK_KEY);
}

static inline void fmql_pinctrl_configure_pin(const pinctrl_soc_pin_t *pin)
{
	uint32_t pinid = GET_PIN(pin->pinmux);
	uint32_t mux = GET_MUX(pin->pinmux) << FMQL_HW_SEL_SHIFT;
	uint32_t misc = pin->misc;
	uint32_t adr = PINCTRL_REG + (pinid * 4);

	syscon_write_reg(SYSCON_DEV, adr, mux | misc);
}

int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t pin_cnt, uintptr_t reg)
{
	fmql_syscon_unlock();
	for (uint8_t i = 0U; i < pin_cnt; i++) {
		fmql_pinctrl_configure_pin(pins++);
	}
	fmql_syscon_lock();
	return 0;
}
