/**
 * @file    wolfssl_settings.h
 * @brief   WolfSSL settings for Zephyr
 * @details This file contains the configuration settings for the WolfSSL library
 * @date   2025-05-09
 *
 * @copyright Copyright (c) 2025 SYSFly Co.
 *
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define ECC_TIMING_RESISTANT
#define WC_RSA_BLINDING

#define HAVE_ECC
#define WOLFSSL_PEM_TO_DER
#define WOLFSSL_DER_TO_PEM
#define WOLFSSL_KEY_GEN
#define WOLFSSL_CERT_GEN
#define WOLFSSL_CERT_REQ
#define WOLFSSL_PKCS512

#ifdef __cplusplus
}
#endif
