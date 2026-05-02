/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Board-specific initialization for Zephyr qemu_cortex_a9.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rtclaw_board_a9, LOG_LEVEL_INF);

static int board_a9_init(void)
{
    LOG_INF("rt-claw board init: qemu_cortex_a9");
    return 0;
}

SYS_INIT(board_a9_init, APPLICATION, 90);
