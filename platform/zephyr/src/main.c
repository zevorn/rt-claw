/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Zephyr application entry point for rt-claw.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "claw/init.h"
#include "osal/claw_kv.h"
#include "claw/shell/shell_commands.h"
#include "platform/board.h"

LOG_MODULE_REGISTER(rtclaw_main, LOG_LEVEL_INF);

int main(void)
{
    board_early_init();

    if (claw_kv_init() == 0) {
        shell_nvs_config_load();
    }
    claw_init();

    /*
     * Zephyr keeps the system running after main() returns.
     * Service threads spawned by claw_init() continue to execute.
     */
    return 0;
}
