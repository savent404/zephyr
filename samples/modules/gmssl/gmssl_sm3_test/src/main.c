/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <string.h>

#include <gmssl/sm3.h>
#include <gmssl/hex.h>

static int hex_compare(const char *expected, const uint8_t *data, size_t len)
{
	char hex_output[2 * len + 1];

	hex_output[2 * len] = '\0';

	for (size_t i = 0; i < len; i++) {
		sprintf(&hex_output[2 * i], "%02x", data[i]);
	}

	if (strcmp(expected, hex_output) == 0) {
		printk("Hex comparison successful: %s\n", hex_output);
	} else {
		printk("Hex comparison failed: expected %s, got %s\n", expected, hex_output);
		return -1;
	}

	return 0;
}

int main(void)
{
	printk("Hello World from minimal!\n");

	/* Test SM3 hash function */
	SM3_CTX ctx;
	uint8_t hash[SM3_DIGEST_SIZE];
	const char *test_data = "abc";

	printk("Testing GmSSL SM3 hash function...\n");

	sm3_init(&ctx);
	sm3_update(&ctx, (uint8_t *)test_data, strlen(test_data));
	sm3_finish(&ctx, hash);

	printk("SM3 hash of '%s': ", test_data);
	for (int i = 0; i < SM3_DIGEST_SIZE; i++) {
		printk("%02x", hash[i]);
	}
	printk("\n");

	/* Expected hash for "abc": 66c7f0f462eeedd9d1f2d46bdc10e4e24167c4875cf2f7a2297da02b8f4ba8e0
	 */
	static const char expected_hash[] =
		"66c7f0f462eeedd9d1f2d46bdc10e4e24167c4875cf2f7a2297da02b8f4ba8e0";

	if (!hex_compare(expected_hash, hash, SM3_DIGEST_SIZE)) {
		printk("SM3 hash test passed!\n");
	} else {
		printk("SM3 hash test failed!\n");
		return -1;
	}

	return 0;
}
