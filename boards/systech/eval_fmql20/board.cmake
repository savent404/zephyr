#
# Copyright (c) 2024 SYSTech Co.
# SPDX-License-Identifier: Apache-2.0
#

board_runner_args(jlink "--device=cortex-a7" "--speed=4000")

include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
