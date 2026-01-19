# SPDX-License-Identifier: Apache-2.0

zephyr_sources_ifdef(CONFIG_DISK_ACCESS disk_access.c)

add_subdirectory_ifdef(CONFIG_DISK_TOOLS disk_tools)
