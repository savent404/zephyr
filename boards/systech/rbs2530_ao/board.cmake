#
# Copyright (c) 2024 SYSTech Co.
# SPDX-License-Identifier: Apache-2.0
#

board_runner_args(jlink "--iface=JTAG"
	"--device=cortex-a7"
	"--speed=4000"
	"--tool-opt=-JTAGConf -1,-1"
	"--no-reset-after-load")

include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
