/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Zephyr Shell integration for rt-claw.
 * Uses shell_set_bypass() to intercept raw input, then dispatches
 * through rt-claw's command tables for slash commands.
 * Non-command text is forwarded to the AI engine.
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/logging/log.h>

/*
 * Zephyr and rt-claw both define SHELL_CMD.
 * We use Zephyr Shell in bypass mode, so undef Zephyr's macro.
 */
#undef SHELL_CMD

#include "osal/claw_os.h"
#include "claw/core/console.h"
#include "claw/shell/shell_cmd.h"
#include "claw/shell/shell_commands.h"
#include "platform/board.h"
#include "claw/services/ai/ai_engine.h"

#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(rtclaw_shell, LOG_LEVEL_INF);

#define AI_REPLY_SIZE  2048

/* ---------- built-in commands ---------- */

static void cmd_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    int common_count = shell_common_command_count();
    int board_count = 0;
    const shell_cmd_t *board_cmds =
        board_platform_commands(&board_count);

    claw_printf("Available commands:\n");
    shell_print_help(shell_common_commands, common_count);
    if (board_count > 0) {
        shell_print_help(board_cmds, board_count);
    }
}

static void cmd_version(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    claw_printf("RT-Claw v%s (Zephyr)\n", "0.2.0");
}

static const shell_cmd_t s_builtin_cmds[] = {
    SHELL_CMD("/help",    cmd_help,    "Show available commands"),
    SHELL_CMD("/version", cmd_version, "Show firmware version"),
};

#define BUILTIN_COUNT  (sizeof(s_builtin_cmds) / sizeof(s_builtin_cmds[0]))

/* ---------- line processing ---------- */

static void process_line(char *line)
{
    if (line[0] == '\0') {
        return;
    }

    /* Slash commands: dispatch through command tables */
    if (line[0] == '/') {
        int argc;
        char *argv[16];

        argc = shell_tokenize(line, argv, 16);
        if (argc == 0) {
            return;
        }

        if (shell_dispatch(s_builtin_cmds, BUILTIN_COUNT, argc, argv)) {
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
        return;
    }

    /* Non-command text: forward to AI engine */
    static char ai_reply[AI_REPLY_SIZE];

    ai_set_channel(AI_CHANNEL_SHELL);
    int ret = ai_chat(line, ai_reply, sizeof(ai_reply));

    if (ret == 0 && ai_reply[0] != '\0') {
        claw_printf("%s\n", ai_reply);
    } else if (ret != 0) {
        claw_printf("AI error: %d (check API key with /ai_status)\n",
                    ret);
    }
}

/* ---------- shell bypass handler ---------- */

static void shell_bypass_cb(const struct shell *sh,
                             uint8_t *data, size_t len,
                             void *user_data)
{
    static char line_buf[256];
    static size_t line_pos;

    (void)sh;
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

            process_line(line_buf);

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
    const struct shell *sh = shell_backend_uart_get_ptr();

    if (!sh) {
        LOG_ERR("No UART shell backend");
        return -1;
    }

    shell_set_bypass(sh, shell_bypass_cb, NULL);
    printk("rt-claw> ");
    return 0;
}

SYS_INIT(rtclaw_shell_init, APPLICATION, 99);
