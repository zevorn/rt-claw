/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Zephyr platform board abstraction.
 */

#include "platform/board.h"
#include "osal/claw_os.h"

void board_early_init(void)
{
    /* Board-specific early init is handled by Zephyr's device model. */
}

const shell_cmd_t *board_platform_commands(int *count)
{
    if (count) {
        *count = 0;
    }
    return NULL;
}
