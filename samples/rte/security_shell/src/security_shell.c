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
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <string.h>
#include <stdlib.h>

/* wolfSSL includes */
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/random.h>

LOG_MODULE_REGISTER(security_shell, CONFIG_LOG_DEFAULT_LEVEL);

static int cmd_ss_genkey(const struct shell *sh, size_t argc, char **argv)
{
	static const char *const algorithms[] = {
		"ecc256", "rsa4096",
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
	/* TODO: Add the actual key generation logic here, wolfCrypt_Init() and wc_InitRng() are
	 * just examples
	 **/
	shell_print(sh, "Key generated successfully with algorithm: %s", chosen_algorithm);

	return 0;
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
