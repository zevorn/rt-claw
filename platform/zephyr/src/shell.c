/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Zephyr Shell integration for rt-claw.
 * Uses shell_set_bypass() to intercept raw input, then dispatches
 * through rt-claw's shell_dispatch() for slash commands and AI fallback.
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/logging/log.h>

/*
 * Zephyr and rt-claw both define SHELL_CMD. Zephyr's is for its
 * command tree; rt-claw's is a convenience initializer for shell_cmd_t.
 * We use Zephyr Shell in bypass mode so we don't need Zephyr's macro.
 */
#undef SHELL_CMD

#include "osal/claw_os.h"
#include "claw/core/console.h"
#include "claw/shell/shell_cmd.h"
#include "claw/shell/shell_commands.h"
#include "platform/board.h"

#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(rtclaw_shell, LOG_LEVEL_INF);

static const struct shell *s_shell_ptr;

/* ---------- shell bypass handler ---------- */

static void process_line(const struct shell *sh, char *line)
{
    int argc;
    char *argv[16];

    argc = shell_tokenize(line, argv, 16);
    if (argc == 0) {
        return;
    }

    int board_count = 0;
    const shell_cmd_t *board_cmds =
        board_platform_commands(&board_count);

    if (shell_dispatch(board_cmds, board_count, argc, argv)) {
        return;
    }

    if (shell_dispatch(shell_common_commands,
                       shell_common_command_count(),
                       argc, argv)) {
        return;
    }

    claw_printf("Unknown command: %s\n", argv[0]);
}

static void shell_bypass_cb(const struct shell *sh,
                             uint8_t *data, size_t len,
                             void *user_data)
{
    static char line_buf[256];
    static size_t line_pos;

    (void)user_data;

    for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];

        if (c == '\r' || c == '\n') {
            if (line_pos == 0) {
                printk("\nrt-claw> ");
                continue;
            }
            line_buf[line_pos] = '\0';
            printk("\n");

            process_line(sh, line_buf);

            line_pos = 0;
            printk("rt-claw> ");
        } else if (c == 0x7f || c == '\b') {
            if (line_pos > 0) {
                line_pos--;
                printk("\b \b");
            }
        } else if (c >= ' ' && line_pos < sizeof(line_buf) - 1) {
            line_buf[line_pos++] = c;
            printk("%c", c);
        }
    }
}

/* ---------- init ---------- */

static int rtclaw_shell_init(void)
{
    s_shell_ptr = shell_backend_uart_get_ptr();
    if (!s_shell_ptr) {
        LOG_ERR("No UART shell backend");
        return -1;
    }

    shell_set_bypass(s_shell_ptr, shell_bypass_cb, NULL);
    printk("rt-claw> ");
    return 0;
}

SYS_INIT(rtclaw_shell_init, APPLICATION, 99);
