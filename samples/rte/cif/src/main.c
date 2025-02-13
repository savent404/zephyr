/**
 * @file main.c
 * @author Liao YuanKai(savent_gate@outlook.com)
 * @brief
 * @version 0.1
 * @date 2025-01-28
 *
 * @copyright Copyright (c) 2025 SYSTech Co.
 *
 */
#include <zephyr/shell/shell.h>
#include <zephyr/kernel.h>
#include <string.h>
#include <stdlib.h>

#include "cif_main.h"

struct context ctx_ = {0};

int main(void)
{
	ctx_.target_role = role_slave;
	slave_start();

	return 0;
}

static void unconditional_switch(int role)
{
	/* Cancel current role */
	if (ctx_.target_role == role_master) {
		master_cancel();
	} else {
		slave_cancel();
	}

	/* Start new role */
	if (role == role_master) {
		master_start();
	} else if (role == role_slave) {
		slave_start();
	}
	ctx_.target_role = role;
}

static int cif_cmd(const struct shell *sh, size_t argc, char **argv)
{
	bool handled = false;

	if (ctx_.target_role == role_master) {
		if (!strcmp(argv[1], "discovery")) {
			ctx_.cmd = CMD_DISCOVERY;
			ctx_.target_sid = atoi(argv[2]);
			handled = true;
		} else if (!strcmp(argv[1], "config")) {
			ctx_.cmd = CMD_CONFIG;
			ctx_.target_sid = atoi(argv[2]);
			handled = true;
		} else if (!strcmp(argv[1], "io")) {
			ctx_.cmd = CMD_IO;
			ctx_.target_sid = atoi(argv[2]);

			/* Handle port */
			if (argc >= 4) {
				ctx_.target_port = atoi(argv[3]);
			} else {
				ctx_.target_port = PORT_ID_IO;
			}

			/* Handle cnt */
			if (argc >= 5) {
				ctx_.target_cnt = atoi(argv[4]);
			} else {
				ctx_.target_cnt = 10;
			}

			/* Handle pps */
			if (argc >= 6 && CIF_IS_ASYNC_PORT(ctx_.target_port)) {
				ctx_.target_pps = atoi(argv[5]);
			} else if (argc >= 6) {
				ctx_.cmd = CMD_NONE;
				shell_print(sh, "Invalid port, try async port");
				return -EINVAL;
			}
			handled = true;
		}
	}

	if (!strcmp(argv[1], "switch")) {
		if (argc == 2) {
			shell_print(sh, "Current role: %s\n",
				    ctx_.target_role == role_master ? "master" : "slave");
			handled = true;
		} else if (!strcmp(argv[2], "slave")) {
			if (argc == 3) {
				ctx_.target_opt = normal;
			} else if (argc == 4 && !strcmp(argv[3], "preempt")) {
				ctx_.target_opt = preempt;
			} else {
				shell_print(sh, "Invalid option, try preempt");
				return -EINVAL;
			}
			unconditional_switch(role_slave);
			handled = true;
		} else if (!strcmp(argv[2], "master")) {
			if (argc == 3) {
				ctx_.target_opt = normal;
			} else if (argc == 4 && !strcmp(argv[3], "preempt")) {
				ctx_.target_opt = preempt;
			} else {
				shell_print(sh, "Invalid option, try preempt");
				return -EINVAL;
			}
			shell_print(sh, "Switch to master role, opt: %s\n",
				    ctx_.target_opt == normal ? "normal" : "preempt");
			unconditional_switch(role_master);
			handled = true;
		} else {
			shell_print(sh, "Invalid role, try slave or master");
			return -EINVAL;
		}
	}

	if (!handled) {
		shell_print(sh, "Unknown subcmd or invalid arguments number");
		shell_print(sh, "cmd: discovery, config, io");
		shell_print(sh, "\tdiscovery <slot>");
		shell_print(sh, "\tconfig <slot>");
		shell_print(sh, "\tio <slot> [port] [cnt] [pps]");
		shell_print(sh, "\tswitch <slave|master> [preempt]");
		return 0;
	}

	return 0;
}

SHELL_CMD_REGISTER(cif, NULL, "Dump version information", cif_cmd);
