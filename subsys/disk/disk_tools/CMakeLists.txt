# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_DISK_TOOLS disk_emmc.c)
zephyr_library_sources_ifdef(CONFIG_DISK_TOOLS disk_tools_shell.c)
zephyr_library_sources_ifdef(CONFIG_DISK_TOOLS_NETWORK disk_tools_network.c)
