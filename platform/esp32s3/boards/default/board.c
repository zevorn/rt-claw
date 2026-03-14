/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ESP32-S3 default board — WiFi + PSRAM real hardware.
 */

#include "claw_board.h"

void board_early_init(void)
{
    wifi_board_early_init();
}

const shell_cmd_t *board_platform_commands(int *count)
{
    return wifi_board_platform_commands(count);
}
