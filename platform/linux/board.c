/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Board abstraction for Linux native platform.
 */

#include "platform/board.h"

#ifdef CONFIG_RTCLAW_TOOL_SCRIPT
#include "platform/scripting.h"

#include <stdio.h>
#include <string.h>

#define PYTHON_CODE_SIZE   256
#define PYTHON_OUTPUT_SIZE 1024

static void cmd_python(int argc, char **argv)
{
    char code[PYTHON_CODE_SIZE];
    char output[PYTHON_OUTPUT_SIZE];
    int off = 0;

    if (argc < 2) {
        claw_printf("Usage: /python <code>\n");
        return;
    }

    code[0] = '\0';
    for (int i = 1; i < argc && off < (int)sizeof(code) - 1; i++) {
        off += snprintf(code + off, sizeof(code) - off,
                        "%s%s", i > 1 ? " " : "", argv[i]);
        if (off >= (int)sizeof(code)) {
            code[sizeof(code) - 1] = '\0';
            break;
        }
    }

    if (claw_platform_run_script("python", code,
                                 output, sizeof(output)) != 0) {
        claw_printf("%s\n",
                    output[0] ? output : "Python execution failed");
        return;
    }

    if (output[0] != '\0') {
        claw_printf("%s", output);
        if (output[strlen(output) - 1] != '\n') {
            claw_printf("\n");
        }
    }
}

static const shell_cmd_t s_linux_commands[] = {
    SHELL_CMD("/python", cmd_python, "Execute a short Python snippet"),
};
#endif

void board_early_init(void)
{
    /* No hardware init needed on Linux */
}

const shell_cmd_t *board_platform_commands(int *count)
{
#ifdef CONFIG_RTCLAW_TOOL_SCRIPT
    *count = SHELL_CMD_COUNT(s_linux_commands);
    return s_linux_commands;
#else
    *count = 0;
    return NULL;
#endif
}
