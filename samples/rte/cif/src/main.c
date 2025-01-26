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
			if (argc >= 4) {
				ctx_.target_port = atoi(argv[3]);
			} else {
				ctx_.target_port = PORT_ID_IO;
			}
			if (argc >= 5) {
				ctx_.target_cnt = atoi(argv[4]);
			} else {
				ctx_.target_cnt = 10;
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
			if (ctx_.target_role != role_slave) {
				ctx_.target_role = role_slave;
				master_cancel();
				slave_start();
			}
			handled = true;
		} else if (!strcmp(argv[2], "master")) {
			if (ctx_.target_role != role_master) {
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
				ctx_.target_role = role_master;
				slave_cancel();
				master_start();
			}
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
		shell_print(sh, "\tio <slot> [port] [cnt]");
		shell_print(sh, "\tswitch <slave|master> [preempt]");
		return 0;
	}

	return 0;
}

SHELL_CMD_REGISTER(cif, NULL, "Dump version information", cif_cmd);
