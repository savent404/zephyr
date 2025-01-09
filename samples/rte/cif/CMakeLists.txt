# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rte_cif)

target_sources_ifndef(CONFIG_CIF_SLAVE app PRIVATE src/main_master.c)
target_sources_ifdef(CONFIG_CIF_SLAVE app PRIVATE src/main_slave.c)
