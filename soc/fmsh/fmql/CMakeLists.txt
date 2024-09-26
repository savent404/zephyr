#
# Copyright (c) 2021 Weidmueller Interface GmbH & Co. KG
# Copyright (c) 2024 SYSTech Co.
#
# SPDX-License-Identifier: Apache-2.0
#

add_subdirectory(${SOC_SERIES})

# This is for access to pinctrl macros
zephyr_include_directories(common)
