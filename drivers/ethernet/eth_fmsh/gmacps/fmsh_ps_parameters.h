/**
 * Copyright (c) 2024 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _FMSH_PS_PARAMETERS_H_
#define _FMSH_PS_PARAMETERS_H_

#define PS_CLK_FREQ 33333333

#define FPS_GMAC0_BASEADDR (0xE0047000)
#define FPS_GMAC1_BASEADDR (0xE0049000)
#define FPS_SLCR_BASEADDR  (0xE0026000)

/* Definitions for peripheral PS_GMAC_0 */
#define FPAR_GMACPS_0_DEVICE_ID 0
#define FPAR_GMACPS_0_BASEADDR  FPS_GMAC0_BASEADDR
#define FPAR_GMACPS_0_SPEED     speed_1000
#define FPAR_GMACPS_0_INTERFACE gmac_path_gmii

/* Definitions for peripheral PS_GMAC_1 */
#define FPAR_GMACPS_1_DEVICE_ID 1
#define FPAR_GMACPS_1_BASEADDR  FPS_GMAC1_BASEADDR
#define FPAR_GMACPS_1_SPEED     speed_1000
#define FPAR_GMACPS_1_INTERFACE gmac_path_gmii

#endif /* protection macro */
