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

LOG_MODULE_REGISTER(rtclaw_main, LOG_LEVEL_INF);

int main(void)
{
    claw_kv_init();
    claw_init();

    /*
     * Zephyr keeps the system running after main() returns.
     * Service threads spawned by claw_init() continue to execute.
     */
    return 0;
}
