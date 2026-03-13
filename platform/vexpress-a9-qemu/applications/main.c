/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Platform entry for QEMU vexpress-a9 / RT-Thread.
 */

#include <rtthread.h>
#include "osal/claw_os.h"
#include "claw/claw_init.h"

int main(void)
{
    claw_log_set_enabled(1);
    claw_init();
    return 0;
}
