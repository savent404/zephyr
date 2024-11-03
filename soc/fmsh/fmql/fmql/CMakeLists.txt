#
# Copyright (c) 2021 Weidmueller Interface GmbH & Co. KG
# Copyright (c) 2024 SYSTech Co.
# SPDX-License-Identifier: Apache-2.0
#

zephyr_sources(soc.c ps_init.c)

zephyr_include_directories(.)

set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/arm/cortex_a_r/scripts/linker.ld CACHE INTERNAL "")
