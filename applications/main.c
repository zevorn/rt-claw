/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>
#include <rtthread.h>

#define RT_CLAW_VERSION "0.1.0"

int main(void)
{
    rt_kprintf("\n");
    rt_kprintf("  ┌─────────────────────────────────────┐\n");
    rt_kprintf("  │         rt-claw v%s              │\n", RT_CLAW_VERSION);
    rt_kprintf("  │  Real-Time Claw on RT-Thread/QEMU   │\n");
    rt_kprintf("  │  Swarm Intelligence for Embedded     │\n");
    rt_kprintf("  └─────────────────────────────────────┘\n");
    rt_kprintf("\n");

    return 0;
}
