# Copyright (c) 2025 SYSTech Co.
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_include_directories(..)
zephyr_include_directories(inc)
zephyr_library_sources(ldp_utils_ze.cpp)
zephyr_library_sources(src/ldp_bc.cpp)
