# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rte_cif)

target_include_directories(app PRIVATE inc)
target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/main_master.c)
target_sources(app PRIVATE src/main_slave.c)
