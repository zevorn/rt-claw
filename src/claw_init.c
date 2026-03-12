/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 */

#include "claw_os.h"
#include "claw_config.h"
#include "claw_init.h"
#include "gateway.h"
#include "scheduler.h"
#include "swarm.h"
#include "net_service.h"
#include "claw_tools.h"
#include "ai_engine.h"
#include "ai_skill.h"
#include "feishu.h"

int claw_init(void)
{
    claw_log_raw("\n");
    claw_log_raw("  +-----------------------------------------+\n");
    claw_log_raw("  |          rt-claw v%s                 |\n", RT_CLAW_VERSION);
    claw_log_raw("  |  Real-Time Claw / Swarm Intelligence    |\n");
    claw_log_raw("  +-----------------------------------------+\n");
    claw_log_raw("\n");

    gateway_init();
    sched_init();
    swarm_init();
    net_service_init();
    swarm_start();
    claw_lcd_init();
    claw_tools_init();
    ai_engine_init();
    ai_skill_init();
    feishu_init();
    feishu_start();

    return CLAW_OK;
}
