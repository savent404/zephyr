/**
 * @file main.c
 * @copyright Copyright (c) 2025 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <string.h>
#include <mbedtls/ssl.h>
#include <mbedtls/error.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>

/*
 * Example CA certificate (PEM format)
 * openssl genrsa -out ca.key 1024
 * openssl req -x509 -new -nodes -key ca.key -sha256 -days 3650 -out ca.crt
 * openssl x509 -in ca.crt -pubkey -noout > ca_pubkey.pem
 */
static const char ca_pubkey[] = "-----BEGIN PUBLIC KEY-----\n"
				"MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDZ0qRFl/Lzc4Zvcak90IbyXFsG\n"
				"zLOa6CWsYWrujdb03FfcxU4slys3fcYruLRc2aDIl/KS5HnZh4FwIuzBdxJWg6LJ\n"
				"J6QsQjjJOyEOUX7evobcP0fUO26QgOVsXnHFDGruD+z6u95jYnMlWOeUhYlb20nH\n"
				"JptB/sI2vk+OJBOFSQIDAQAB\n"
				"-----END PUBLIC KEY-----";

/*
 * Example client certificate (PEM format)
 * echo "Hello,World" > yourfile.txt
 */
static const unsigned char file_content[] = {0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x2c,
					     0x57, 0x6f, 0x72, 0x6c, 0x64, 0x0a};

/*
 * penssl dgst -sha256 -sign ca.key -out file.sig yourfile.txt
 * xxd -i file.sig
 */
static const unsigned char file_sig[] = {
	0x2c, 0x05, 0xf0, 0x43, 0xb8, 0xfb, 0x36, 0xeb, 0x7f, 0x7c, 0x4a, 0xae, 0x09, 0x96, 0x71,
	0x0b, 0x1e, 0xb3, 0x08, 0xdb, 0xa1, 0x32, 0x75, 0x93, 0xc8, 0x39, 0x07, 0x2e, 0xdf, 0xba,
	0xf9, 0x13, 0xbc, 0x4b, 0xea, 0xc2, 0x54, 0x98, 0x7b, 0x14, 0x36, 0x5b, 0xb1, 0xcb, 0x11,
	0x80, 0x36, 0xa2, 0xdd, 0x36, 0x82, 0x7d, 0x88, 0xa5, 0xe3, 0x67, 0xce, 0x64, 0xfb, 0xa0,
	0xc1, 0x25, 0x73, 0x10, 0xb3, 0x6a, 0x03, 0xb4, 0x80, 0xeb, 0x7e, 0x78, 0xb6, 0x0b, 0x9c,
	0x1e, 0x89, 0x9d, 0xf8, 0xce, 0xbc, 0x73, 0xc8, 0xab, 0x6f, 0xed, 0x6e, 0x40, 0x7b, 0xe5,
	0xf6, 0xce, 0xd1, 0xa8, 0x24, 0x2f, 0x1b, 0xdd, 0x49, 0x7d, 0x1f, 0x4e, 0x3c, 0x79, 0x9d,
	0x99, 0xe2, 0x11, 0xbd, 0x34, 0x69, 0x5c, 0xa5, 0x11, 0xc3, 0xed, 0x72, 0xd9, 0x16, 0x9d,
	0xdb, 0xde, 0x95, 0x8d, 0x6d, 0x99, 0xa1, 0x78};

void main(void)
{
	int ret;
	char hash[32];
	mbedtls_pk_context pk;
	mbedtls_md_context_t md_ctx;
	char error_buf[100] = "null";

	mbedtls_pk_init(&pk);

	/* Load the public key */
	ret = mbedtls_pk_parse_public_key(&pk, (const unsigned char *)ca_pubkey,
					  strlen(ca_pubkey) + 1);
	if (ret != 0) {
		mbedtls_strerror(ret, error_buf, 100);
		printk("Failed to parse public key: %s\n", error_buf);
		return;
	}

	/* Hash the file content */
	mbedtls_md_init(&md_ctx);
	mbedtls_md_setup(&md_ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
	mbedtls_md_starts(&md_ctx);
	mbedtls_md_update(&md_ctx, file_content, sizeof(file_content));
	mbedtls_md_finish(&md_ctx, hash);
	mbedtls_md_free(&md_ctx);

	/* Verify the signature */
	ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), file_sig,
				sizeof(file_sig));

	if (ret != 0) {
		mbedtls_strerror(ret, error_buf, 100);
		printk("Failed to verify signature: %s\n", error_buf);
	} else {
		printk("Signature verified successfully\n");
	}

	mbedtls_pk_free(&pk);
}
