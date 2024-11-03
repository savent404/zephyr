/*
 * Copyright (c) 2021 Weidmueller Interface GmbH & Co. KG
 * Copyright (c) 2024 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _SOC__H_
#define _SOC__H_

#ifndef _ASMLANGUAGE

/*
 * The following definitions are required for the inclusion of the CMSIS
 * Common Peripheral Access Layer for aarch32 Cortex-A CPUs:
 */

#define __CORTEX_A 7U

#ifdef __cplusplus
extern "C" {
#endif

int fmsh_psoc_ps_init(void);

#ifdef __cplusplus
}
#endif

#endif /* !_ASMLANGUAGE */

#endif /* _SOC__H_ */
