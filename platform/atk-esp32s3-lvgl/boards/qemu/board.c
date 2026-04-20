/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * QEMU virtual board — network via OpenCores Ethernet (sdkconfig).
 */

#include "platform/board.h"
#include <stddef.h>

void board_early_init(void)
{
    /* Ethernet is auto-configured by ESP-IDF via sdkconfig */
}

const shell_cmd_t *board_platform_commands(int *count)
{
    *count = 0;
    return NULL;
}
