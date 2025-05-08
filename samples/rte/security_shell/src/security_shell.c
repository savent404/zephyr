/**
 * @file security_shell.c
 * @author Liao,YuanKai(savent_gate@outlook.com)
 * @brief
 * @version 0.1
 * @date 2025-05-06
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "security_shell.h"
#include "wolfssl_settings.h"
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <string.h>
#include <stdlib.h>

/* wolfSSL includes */
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/ecc.h> /* Required for ecc_key and ECC_SECP256R1 */
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/asn.h>

LOG_MODULE_REGISTER(security_shell, CONFIG_LOG_DEFAULT_LEVEL);

#define PEM_BUFFER_SIZE 8192

static int initialize_rng(WC_RNG **rng)
{
	/* Initialize the random number generator */
	*rng = (WC_RNG *)k_malloc(sizeof(WC_RNG));
	/* Check if memory allocation was successful */
	if (*rng == NULL) {
		LOG_ERR("Failed to allocate memory for RNG");
		return -ENOMEM;
	}
	int ret = wc_InitRng(*rng);

	if (ret != 0) {
		LOG_ERR("RNG initialization failed with error: %d", ret);
		k_free(*rng);
		*rng = NULL;
		return -EIO;
	}
	return 0;
}

static void cleanup_rng(WC_RNG *rng)
{
	if (rng != NULL) {
		int ret = wc_FreeRng(rng);

		if (ret != 0) {
			LOG_ERR("Failed to free RNG: %d", ret);
		}
		k_free(rng);
	}
}

static int generate_ecc_key(const struct shell *sh, WC_RNG *rng)
{
	ecc_key *key = (ecc_key *)k_malloc(sizeof(ecc_key));

	if (key == NULL) {
		shell_print(sh, "Failed to allocate memory for ECC key");
		return -ENOMEM;
	}

	int ret = 0;
	byte derPriv[512];
	byte derPub[256];
	byte pemBuffer[PEM_BUFFER_SIZE];
	word32 derPrivLen;
	word32 derPubLen;
	word32 pemLen;

	/* Initialize wolfCrypt */
	wolfCrypt_Init();

	/* Initialize the ECC key */
	ret = wc_ecc_init(key);
	if (ret != 0) {
		shell_print(sh, "Failed to initialize ECC key: %d", ret);
		k_free(key);
		return -EIO;
	}
	/* Generate the ECC key */
	ret = wc_ecc_make_key_ex(rng, 32, key, ECC_SECP256R1);
	if (ret != 0) {
		shell_print(sh, "Failed to generate ECC key: %d", ret);
		wc_ecc_free(key);
		k_free(key);
		return -EIO;
	}

	/* Convert the ECC private key to DER format */
	derPrivLen = wc_EccPrivateKeyToDer(key, derPriv, sizeof(derPriv));
	if (derPrivLen <= 0) {
		shell_print(sh, "Failed to convert ECC private key to DER: %d", derPrivLen);
		wc_ecc_free(key);
		k_free(key);
		return -EIO;
	}
	/* Convert the ECC private key to PEM format */
	pemLen =
		wc_DerToPem(derPriv, derPrivLen, pemBuffer, sizeof(pemBuffer), ECC_PRIVATEKEY_TYPE);
	if (pemLen <= 0) {
		shell_print(sh, "Failed to convert ECC private key to PEM: %d", pemLen);
		wc_ecc_free(key);
		k_free(key);
		return -EIO;
	}
	pemBuffer[pemLen] = '\0'; /* Null-terminate the PEM buffer */
	shell_print(sh, "%s", pemBuffer);

	/* Convert the ECC public key to DER format */
	derPubLen = wc_EccPublicKeyToDer(key, derPub, sizeof(derPub), 1);
	if (derPubLen <= 0) {
		shell_print(sh, "Failed to convert ECC public key to DER: %d", derPubLen);
		wc_ecc_free(key);
		k_free(key);
		return -EIO;
	}
	/* Convert the ECC public key to PEM format */
	pemLen = wc_DerToPem(derPub, derPubLen, pemBuffer, sizeof(pemBuffer), ECC_PUBLICKEY_TYPE);
	if (pemLen <= 0) {
		shell_print(sh, "Failed to convert ECC public key to PEM: %d", pemLen);
		wc_ecc_free(key);
		k_free(key);
		return -EIO;
	}
	pemBuffer[pemLen] = '\0'; /* Null-terminate the PEM buffer */
	shell_print(sh, "%s", pemBuffer);

	/* Free the ECC key */
	wc_ecc_free(key);
	k_free(key);
	return 0;
}

static int cmd_ss_genkey(const struct shell *sh, size_t argc, char **argv)
{
	static const char *const algorithms[] = {
		"ecc256",
		/* Add more algorithms as needed */
	};
	const char *chosen_algorithm = NULL;
	int chosen_algorithm_index = -1;

	if (argc == 1) {
		chosen_algorithm = algorithms[0]; /* Default to the first algorithm */
	} else if (argc == 2) {
		chosen_algorithm = argv[1];
	} else {
		shell_print(sh, "Invalid arguments number");
		return -EINVAL;
	}

	/* determine the chosen algorithm */
	for (int i = 0; i < ARRAY_SIZE(algorithms); i++) {
		if (strcmp(chosen_algorithm, algorithms[i]) == 0) {
			chosen_algorithm_index = i;
			break;
		}
	}

	if (chosen_algorithm_index == -1) {
		shell_print(sh, "Invalid algorithm: %s", chosen_algorithm);
		return -EINVAL;
	}

	LOG_INF("Generating key with algorithm: %s[%d]", chosen_algorithm, chosen_algorithm_index);

	WC_RNG *rng = NULL;

	int ret = initialize_rng(&rng);

	if (ret != 0) {
		shell_print(sh, "Failed to initialize RNG: %d", ret);
		return ret;
	}

	if (chosen_algorithm_index == 0) {
		ret = generate_ecc_key(sh, rng);
	} else {
		shell_print(sh, "Unsupported algorithm index: %d", chosen_algorithm_index);
		return -EINVAL;
	}

	cleanup_rng(rng);
	return ret;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_ss_cmds,
			       SHELL_CMD_ARG(genkey, NULL,
					     "Generate key\n"
					     "Usage: genkey [algorithm: rsa4096|ecc256...etc]",
					     cmd_ss_genkey, 1, 1),
			       /* NOTE: Add more subcommands here as needed */
			       /*...*/
			       SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(security, &sub_ss_cmds, "Security shell commands", NULL);
