# Copyright (c) 2024 SYSTech Co.
# SPDX-License-Identifier: Apache-2.0

zephyr_library_sources_ifdef(CONFIG_ETH_FMQL eth_fmsh_gmac.c)
zephyr_library_sources_ifdef(CONFIG_ETH_FMQL gmacps/fmsh_gmac_lib.c)
zephyr_library_sources_ifdef(CONFIG_ETH_FMQL gmacps/fmsh_gmac_hw.c)
zephyr_library_sources_ifdef(CONFIG_ETH_FMQL gmacps/fmsh_phy.c)
zephyr_library_sources_ifdef(CONFIG_ETH_FMQL gmacps/motorcomm_yt8521.c)
zephyr_library_sources_ifdef(CONFIG_ETH_FMQL gmacps/fmsh_mdio.c)
add_compile_options(-Wall -Wunused-function -Wunused-variable)
