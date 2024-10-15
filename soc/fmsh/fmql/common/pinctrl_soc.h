/*
 * Copyright (c) Henrik Brix Andersen <henrik@brixandersen.dk>
 * Copyright (c) SYSTech Co.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>
#include <zephyr/types.h>
#include <zephyr/dt-bindings/pinctrl/fmql-pinctrl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint32_t pinmux;
	uint32_t misc;
} pinctrl_soc_pin_t;

#define Z_PINCTRL_PINMUX(node_id) DT_PROP(node_id, pinmux)

#define Z_PINCTRL_MISC(node_id)                                                                    \
	(DT_PROP(node_id, bias_high_impedance) << FMQL_HW_HZ_EN_SHIFT |                            \
	 DT_PROP(node_id, bias_pull_up) << FMQL_HW_PULLUP_SHIFT |                                  \
	 DT_PROP(node_id, bias_pull_down) << FMQL_HW_PULLDOWN_SHIFT |                              \
	 (DT_PROP(node_id, drive_strength) / 4 - 1) << FMQL_HW_STRENGTH_SHIFT |                    \
	 DT_PROP(node_id, input_disable) << FMQL_HW_RECV_DIS_SHIFT |                               \
	 DT_PROP(node_id, io_type) << FMQL_HW_TYPE_SHIFT |                                         \
	 DT_PROP(node_id, hystr) << FMQL_HW_HYST_EN_SHIFT |                                        \
	 DT_PROP(node_id, keeper) << FMQL_HW_KEEPER_SHIFT)
/**
 * @brief Utility macro to initialize each pin.
 *
 * @param node_id Node identifier.
 * @param state_prop State property name.
 * @param idx State property entry index.
 */
#define Z_PINCTRL_STATE_PIN_INIT(node_id, state_prop, idx)                                         \
	{.pinmux = Z_PINCTRL_PINMUX(DT_PROP_BY_IDX(node_id, state_prop, idx)),                     \
	 .misc = Z_PINCTRL_MISC(DT_PROP_BY_IDX(node_id, state_prop, idx))},

#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop)                                                   \
	{DT_FOREACH_PROP_ELEM(node_id, prop, Z_PINCTRL_STATE_PIN_INIT)}

#ifdef __cplusplus
}
#endif
