/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 */

#include "claw_os.h"
#include "claw_config.h"
#include "claw_init.h"
#include "gateway.h"
#include "net_service.h"
#include "claw_tools.h"
#include "ai_engine.h"

#include <stdio.h>

#ifdef CONFIG_CLAW_SCHED_ENABLE
#include "scheduler.h"
#endif
#ifdef CONFIG_CLAW_SWARM_ENABLE
#include "swarm.h"
#endif
#ifdef CONFIG_CLAW_SKILL_ENABLE
#include "ai_skill.h"
#endif
#ifdef CONFIG_CLAW_FEISHU_ENABLE
#include "feishu.h"
#endif

static void ai_boot_test_thread(void *arg)
{
    (void)arg;
    char *buf = claw_malloc(512);
    if (!buf) {
        return;
    }

    claw_log_raw("  [boot] Testing AI connection ...\n");
    if (ai_chat_raw("Report your status in one short sentence. "
                    "Include your name, platform, and that "
                    "you are online.",
                    buf, 512) == CLAW_OK) {
        claw_log_raw("  [boot] AI> %s\n", buf);
    } else {
        claw_log_raw("  [boot] AI test failed: %s\n", buf);
    }
    claw_free(buf);
}

int claw_init(void)
{
    claw_log_raw("\n");
    claw_log_raw("  +-----------------------------------------+\n");
    claw_log_raw("  |          rt-claw v%s                 |\n", RT_CLAW_VERSION);
    claw_log_raw("  |  Real-Time Claw / Swarm Intelligence    |\n");
    claw_log_raw("  +-----------------------------------------+\n");
    claw_log_raw("\n");

    gateway_init();

#ifdef CONFIG_CLAW_SCHED_ENABLE
    sched_init();
#endif
#ifdef CONFIG_CLAW_SWARM_ENABLE
    swarm_init();
#endif

    net_service_init();

#ifdef CONFIG_CLAW_SWARM_ENABLE
    swarm_start();
#endif
#ifdef CONFIG_CLAW_LCD_ENABLE
    claw_lcd_init();
#endif

    claw_tools_init();
    ai_engine_init();

#ifdef CONFIG_CLAW_SKILL_ENABLE
    ai_skill_init();
#endif
#ifdef CONFIG_CLAW_FEISHU_ENABLE
    feishu_init();
    feishu_start();
#endif

    /* AI connectivity test — run async to avoid blocking boot */
    claw_thread_create("ai_test", ai_boot_test_thread, NULL, 4096, 20);

    return CLAW_OK;
}
