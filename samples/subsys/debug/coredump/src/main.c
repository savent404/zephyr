/*
 * Copyright (c) 2026 SYSTech Co.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/debug/coredump.h>
#include <zephyr/shell/shell.h>
#include <string.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(coredump_test, LOG_LEVEL_DBG);

/*
 * Example override for the coredump eMMC backend watchdog feed hook.
 * Replace printk with your actual watchdog feed call.
 *
 * Note: The coredump backend may call this frequently; do any throttling
 * (time-based, count-based, etc.) here.
 */
void coredump_watchdog_feed(void)
{
	static uint32_t last_ms;
	uint32_t now = k_uptime_get_32();

	if ((now - last_ms) < 500U) {
		return;
	}
	last_ms = now;
	LOG_INF("%s: watchdog feed.", __func__);
}

/* Test modes */
enum test_mode {
	TEST_MODE_NULL_POINTER,
	TEST_MODE_DIVIDE_BY_ZERO,
	TEST_MODE_UNALIGNED_ACCESS,
	TEST_MODE_STACK_OVERFLOW,
	TEST_MODE_INVALID_INSTRUCTION,
	TEST_MODE_MAX
};

/* Test mode names for shell command */
static const char *const test_mode_names[] = {
	"null", "divzero", "unaligned", "stackoverflow", "invalidinst",
};

/* Configuration - removed auto-trigger settings */

/**
 * @brief Trigger null pointer dereference fault
 */
static void trigger_null_pointer_fault(void)
{
	volatile uint32_t *null_ptr = NULL;

	LOG_ERR("Triggering NULL pointer dereference fault...");
	k_msleep(500);

	/* This will cause a data abort */
	*null_ptr = 0xDEADBEEF;

	/* Should never reach here */
	LOG_ERR("ERROR: Failed to trigger null pointer fault!");
}

/**
 * @brief Trigger divide by zero fault
 */
static void trigger_divide_by_zero_fault(void)
{
	volatile int divisor = 0;
	volatile int result;

	LOG_ERR("Triggering divide by zero fault...");
	k_msleep(500);

	/* This will cause an undefined instruction or data abort */
	result = 100 / divisor;

	/* Should never reach here */
	LOG_ERR("ERROR: Failed to trigger divide by zero fault! Result: %d", result);
}

/**
 * @brief Trigger unaligned memory access fault
 */
static void trigger_unaligned_access_fault(void)
{
	/* Create unaligned address */
	uint8_t buffer[8] __aligned(4);
	volatile uint32_t *unaligned_ptr = (uint32_t *)(buffer + 1);

	LOG_ERR("Triggering unaligned memory access fault...");
	k_msleep(500);

	/* This may cause an alignment fault on strict ARM systems */
	*unaligned_ptr = 0x12345678;

	/* Should never reach here */
	LOG_ERR("ERROR: Failed to trigger unaligned access fault!");
}

/**
 * @brief Recursive function to trigger stack overflow
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winfinite-recursion"
static void recursive_overflow(int depth)
{
	/* Create stack pressure with local buffer */
	volatile uint8_t stack_buffer[256];

	/* Use the buffer to prevent optimization */
	memset((void *)stack_buffer, depth & 0xFF, sizeof(stack_buffer));

	LOG_DBG("Recursion depth: %d", depth);

	/* Recurse infinitely */
	recursive_overflow(depth + 1);
}
#pragma GCC diagnostic pop

/**
 * @brief Trigger stack overflow fault
 */
static void trigger_stack_overflow_fault(void)
{
	LOG_ERR("Triggering stack overflow fault...");
	k_msleep(500);

	/* Start recursive calls */
	recursive_overflow(0);

	/* Should never reach here */
	LOG_ERR("ERROR: Failed to trigger stack overflow fault!");
}

/**
 * @brief Trigger invalid instruction fault via function pointer
 */
static void trigger_invalid_instruction_fault(void)
{
	/* Point to invalid memory region */
	void (*invalid_func)(void) = (void (*)(void))0xFFFFFFFF;

	LOG_ERR("Triggering invalid instruction fault...");
	k_msleep(500);

	/* This will cause a prefetch abort */
	invalid_func();

	/* Should never reach here */
	LOG_ERR("ERROR: Failed to trigger invalid instruction fault!");
}

/**
 * @brief Execute the selected test
 */
static void execute_test(enum test_mode mode)
{
	LOG_INF("========================================");
	LOG_INF("Triggering Coredump Test");
	LOG_INF("========================================");

	switch (mode) {
	case TEST_MODE_NULL_POINTER:
		LOG_INF("Test: NULL Pointer Dereference");
		k_msleep(100);
		trigger_null_pointer_fault();
		break;

	case TEST_MODE_DIVIDE_BY_ZERO:
		LOG_INF("Test: Divide by Zero");
		k_msleep(100);
		trigger_divide_by_zero_fault();
		break;

	case TEST_MODE_UNALIGNED_ACCESS:
		LOG_INF("Test: Unaligned Memory Access");
		k_msleep(100);
		trigger_unaligned_access_fault();
		break;

	case TEST_MODE_STACK_OVERFLOW:
		LOG_INF("Test: Stack Overflow");
		k_msleep(100);
		trigger_stack_overflow_fault();
		break;

	case TEST_MODE_INVALID_INSTRUCTION:
		LOG_INF("Test: Invalid Instruction");
		k_msleep(100);
		trigger_invalid_instruction_fault();
		break;

	default:
		LOG_ERR("Unknown test mode: %d", mode);
		break;
	}

	/* If we reach here, the fault was not triggered */
	LOG_ERR("========================================");
	LOG_ERR("Test completed without triggering fault!");
	LOG_ERR("Coredump may not be generated.");
	LOG_ERR("========================================");
}

#ifdef CONFIG_SHELL
/**
 * @brief Shell command to trigger coredump test
 */
static int cmd_coredump_test(const struct shell *sh, size_t argc, char **argv)
{
	enum test_mode mode = TEST_MODE_NULL_POINTER;

	if (argc > 1) {
		/* Parse test mode from argument */
		bool found = false;

		for (int i = 0; i < TEST_MODE_MAX; i++) {
			if (strcmp(argv[1], test_mode_names[i]) == 0) {
				mode = i;
				found = true;
				break;
			}
		}

		if (!found) {
			shell_error(sh, "Unknown test mode: %s", argv[1]);
			shell_print(sh, "Available modes:");
			shell_print(sh, "  null        - NULL pointer dereference");
			shell_print(sh, "  divzero     - Divide by zero");
			shell_print(sh, "  unaligned   - Unaligned memory access");
			shell_print(sh, "  stackoverflow - Stack overflow");
			shell_print(sh, "  invalidinst - Invalid instruction");
			return -EINVAL;
		}
	}

	shell_warn(sh, "!!! WARNING: System will crash in 2 seconds !!!");
	shell_warn(sh, "Triggering %s fault...", test_mode_names[mode]);
	k_msleep(2000);

	/* Execute the test - this will crash the system */
	execute_test(mode);

	/* Should never reach here */
	return 0;
}

/**
 * @brief Shell command to list available test modes
 */
static int cmd_coredump_test_list(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "Available coredump test modes:");
	shell_print(sh, "");
	shell_print(sh, "  %-15s - %s", "null", "NULL pointer dereference (data abort)");
	shell_print(sh, "  %-15s - %s", "divzero", "Divide by zero");
	shell_print(sh, "  %-15s - %s", "unaligned", "Unaligned memory access");
	shell_print(sh, "  %-15s - %s", "stackoverflow", "Stack overflow");
	shell_print(sh, "  %-15s - %s", "invalidinst", "Invalid instruction (prefetch abort)");
	shell_print(sh, "");
	shell_print(sh, "Usage: coredump test <mode>");
	shell_print(sh, "Example: coredump test null");

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_coredump_test,
			       SHELL_CMD_ARG(list, NULL, "List available test modes",
					     cmd_coredump_test_list, 1, 0),
			       SHELL_SUBCMD_SET_END);

SHELL_CMD_ARG_REGISTER(coredump_test, &sub_coredump_test,
		       "Trigger coredump test [mode]\n"
		       "Modes: null, divzero, unaligned, stackoverflow, invalidinst\n"
		       "Use 'coredump_test list' to see all modes",
		       cmd_coredump_test, 1, 1);

#endif /* CONFIG_SHELL */

/**
 * @brief Main application entry point
 */
int main(void)
{
	LOG_INF("========================================");
	LOG_INF("Coredump Test Application");
	LOG_INF("========================================");

#ifdef CONFIG_SHELL
	LOG_INF("Shell enabled - Interactive mode");
	LOG_INF("Use 'coredump_test <mode>' to trigger a fault");
	LOG_INF("Use 'coredump_test list' to see available modes");
	LOG_INF("Example: coredump_test null");
#else
	LOG_WRN("Shell not enabled - No interactive testing available");
	LOG_WRN("Enable CONFIG_SHELL in prj.conf for interactive mode");
#endif

	LOG_INF("========================================");
	LOG_INF("System ready. Waiting for commands...");

	/* Main loop - just keep the system running */
	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
