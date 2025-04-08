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
	/* log needs to be initialized first */
	k_msleep(100);

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
	enum {
		_cmd_none = CMD_NONE,
		_cmd_discovery = CMD_DISCOVERY,
		_cmd_config = CMD_CONFIG,
		_cmd_io = CMD_IO,
		_cmd_switch = CMD_SWITCH,
		_cmd_open = CMD_OPEN,
		_cmd_close = CMD_CLOSE,
		_cmd_perf = CMD_PERF,
	} cmd = _cmd_none;
	const char *subcmd = argv[1];
	static const char *const subcmd_list[] = {
		"discovery", "config", "io", "switch", "open", "close", "perf",
	};

	for (int i = 0; i < ARRAY_SIZE(subcmd_list); i++) {
		if (!strcmp(subcmd, subcmd_list[i])) {
			cmd = i + 1;
			break;
		}
	}

	switch (cmd) {
	case _cmd_discovery: {
		if (ctx_.target_role != role_master) {
			shell_print(sh, "Only master can discovery");
			return -EINVAL;
		}
		if (argc != 3) {
			shell_print(sh, "Invalid arguments number");
			return -EINVAL;
		}
		ctx_.cmd = CMD_DISCOVERY;
		ctx_.target_sid = atoi(argv[2]);
		handled = true;
	} break;
	case _cmd_config: {
		if (ctx_.target_role != role_master) {
			shell_print(sh, "Only master can config");
			return -EINVAL;
		}
		if (argc != 3) {
			shell_print(sh, "Invalid arguments number");
			return -EINVAL;
		}
		ctx_.cmd = CMD_CONFIG;
		ctx_.target_sid = atoi(argv[2]);
		handled = true;
	} break;
	case _cmd_io: {
		if (ctx_.target_role != role_master) {
			shell_print(sh, "Only master can io");
			break;
			return -EINVAL;
		}
		if (argc < 3) {
			shell_print(sh, "Invalid arguments number");
			break;
		}
		ctx_.cmd = CMD_IO;
		ctx_.target_sid = atoi(argv[2]);

		/* Handle port */
		if (argc >= 4) {
			ctx_.target_port = atoi(argv[3]);
		} else {
			ctx_.target_port = PORT_ID_IO;
		}

		/* Handle length */
		if (argc >= 5) {
			ctx_.initial_data_len = atoi(argv[4]);
		} else {
			ctx_.initial_data_len = 32;
		}

		/* Handle duration */
		if (argc >= 6) {
			ctx_.target_duration = atoi(argv[4]) * 1000;
		} else {
			ctx_.target_duration = 3 * 1000;
		}

		/* Handle pps */
		if (argc >= 7 && CIF_IS_ASYNC_PORT(ctx_.target_port)) {
			ctx_.target_pps = atoi(argv[5]);
		} else if (argc >= 6) {
			ctx_.cmd = CMD_NONE;
			shell_print(sh, "Invalid port, try async port");
			return -EINVAL;
		}
		handled = true;
	} break;
	case _cmd_switch: {
		if (argc < 3) {
			shell_print(sh, "Current role: %s\n",
				    ctx_.target_role == role_master ? "master" : "slave");
			shell_print(sh, "Preempt: %s\n", ctx_.target_opt == normal ? "no" : "yes");
			handled = true;
			break;
		}

		bool is_master = !strcmp(argv[2], "master");
		bool is_slave = !strcmp(argv[2], "slave");

		if (!is_master && !is_slave) {
			shell_print(sh, "Invalid role, try slave or master");
			break;
		}

		ctx_.target_opt = normal;
		ctx_.target_bus = bus_low;
		ctx_.perf_mode = false;
		for (int i = 3; i < argc; i++) {
			if (!strcmp(argv[i], "preempt")) {
				ctx_.target_opt = preempt;
				break;
			} else if (!strcmp(argv[i], "bus_low")) {
				ctx_.target_bus = bus_low;
			} else if (!strcmp(argv[i], "bus_high")) {
				ctx_.target_bus = bus_high;
			} else {
				shell_print(sh, "Invalid option: %s", argv[i]);
				return -EINVAL;
			}
		}

		handled = true;
		unconditional_switch(is_master ? role_master : role_slave);
		break;
	}
	case _cmd_open: {
		if (ctx_.target_role != role_master) {
			shell_print(sh, "Only master can open ports");
			return -EINVAL;
		}
		if (argc < 4) {
			shell_print(sh, "Invalid arguments number");
			return -EINVAL;
		}
		ctx_.cmd = CMD_OPEN;
		ctx_.target_sid = atoi(argv[2]);
		ctx_.target_port = atoi(argv[3]);

		/* Handle initial data */
		if (argc >= 5) {
			strncpy((char *)ctx_.initial_data, argv[4], sizeof(ctx_.initial_data) - 1);
			ctx_.initial_data_len = strlen(argv[4]);
		} else {
			/* Default initial data */
			strcpy((char *)ctx_.initial_data, "init");
			ctx_.initial_data_len = 4;
		}

		/* Handle check response option */
		if (argc >= 6 && !strcmp(argv[5], "check")) {
			ctx_.check_response = true;
		} else {
			ctx_.check_response = false;
		}

		handled = true;
	} break;
	case _cmd_close: {
		if (ctx_.target_role != role_master) {
			shell_print(sh, "Only master can close ports");
			return -EINVAL;
		}
		if (argc != 4) {
			shell_print(sh, "Invalid arguments number");
			return -EINVAL;
		}
		ctx_.cmd = CMD_CLOSE;
		ctx_.target_sid = atoi(argv[2]);
		ctx_.target_port = atoi(argv[3]);
		handled = true;
	} break;
	case _cmd_perf: {
		if (argc != 3) {
			shell_print(sh, "Invalid arguments number");
			return -EINVAL;
		}
		if (!strcmp(argv[2], "on")) {
			ctx_.perf_mode = true;
		} else if (!strcmp(argv[2], "off")) {
			ctx_.perf_mode = false;
		} else {
			shell_print(sh, "Invalid arguments");
			return -EINVAL;
		}
		handled = true;
	} break;
	case _cmd_none:
	default:
		break;
	}

	if (!handled) {
		shell_print(sh, "Unknown subcmd or invalid arguments number");
		shell_print(sh, "cmd: discovery, config, io, open, close");
		shell_print(sh, "\tdiscovery <slot>");
		shell_print(sh, "\tconfig <slot>");
		shell_print(sh, "\tio <slot> [port] [length:bytes] [duration:sec] [pps]");
		shell_print(sh, "\tswitch <slave|master> [preempt]");
		shell_print(sh, "\topen <slot> <port> [initial_data] [check]");
		shell_print(sh, "\tclose <slot> <port>");
		shell_print(sh, "\tperf on|off");
		return 0;
	}

	return 0;
}

SHELL_CMD_REGISTER(cif, NULL, "Dump version information", cif_cmd);
