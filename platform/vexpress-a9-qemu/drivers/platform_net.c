/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * vexpress-a9 network hooks for rt-claw.
 */

#include "claw/platform_net.h"

#include "drv_smc911x.h"

void claw_platform_net_prepare(void)
{
    smc911x_link_up();
}
