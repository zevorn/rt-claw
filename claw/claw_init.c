/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "osal/claw_os.h"
#include "claw_config.h"
#include "claw/claw_init.h"
#include "claw/core/claw_service.h"
#include "claw/core/gateway.h"
#include "claw/services/net/net_service.h"
#include "claw/tools/claw_tools.h"
#include "claw/services/ai/ai_engine.h"

#include <stdio.h>

#ifdef CONFIG_RTCLAW_SCHED_ENABLE
#include "claw/core/scheduler.h"
#endif
#ifdef CONFIG_RTCLAW_SWARM_ENABLE
#include "claw/services/swarm/swarm.h"
#endif
#ifdef CONFIG_RTCLAW_SKILL_ENABLE
#include "claw/services/ai/ai_skill.h"
#endif
#ifdef CONFIG_RTCLAW_HEARTBEAT_ENABLE
#include "claw/core/heartbeat.h"
#endif
#ifdef CONFIG_RTCLAW_FEISHU_ENABLE
#include "claw/services/im/feishu.h"
#endif
#ifdef CONFIG_RTCLAW_TELEGRAM_ENABLE
#include "claw/services/im/telegram.h"
#endif

#define TAG "init"

/*
 * Service table — defines boot order and lifecycle.
 * Phase 1: all init() calls in order (dependencies flow top-to-bottom).
 * Phase 2: start() for services that have a runtime phase.
 */
static const claw_service_t s_services[] = {
    { "gateway",     gateway_init,      NULL,          NULL },
#ifdef CONFIG_RTCLAW_SCHED_ENABLE
    { "sched",       sched_init,        NULL,          NULL },
#endif
#ifdef CONFIG_RTCLAW_SWARM_ENABLE
    { "swarm",       swarm_init,        swarm_start,   NULL },
#endif
    { "net",         net_service_init,  NULL,          NULL },
#ifdef CONFIG_RTCLAW_LCD_ENABLE
    { "lcd",         claw_lcd_init,     NULL,          NULL },
#endif
    { "tools",       claw_tools_init,   NULL,          NULL },
    { "ai_engine",   ai_engine_init,    NULL,          NULL },
#ifdef CONFIG_RTCLAW_SKILL_ENABLE
    { "ai_skill",    ai_skill_init,     NULL,          NULL },
#endif
#ifdef CONFIG_RTCLAW_HEARTBEAT_ENABLE
    { "heartbeat",   heartbeat_init,    NULL,          NULL },
#endif
#ifdef CONFIG_RTCLAW_FEISHU_ENABLE
    { "feishu",      feishu_init,       feishu_start,  NULL },
#endif
#ifdef CONFIG_RTCLAW_TELEGRAM_ENABLE
    { "telegram",    telegram_init,     telegram_start, NULL },
#endif
};

#define SERVICE_COUNT (sizeof(s_services) / sizeof(s_services[0]))

#ifdef CONFIG_RTCLAW_AI_BOOT_TEST
#if !defined(CONFIG_RTCLAW_FEISHU_ENABLE) && \
    !defined(CONFIG_RTCLAW_TELEGRAM_ENABLE)
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
#endif
#endif

int claw_init(void)
{
    claw_log_raw("\n");
    claw_log_raw("  +-----------------------------------------+\n");
    claw_log_raw("  |          rt-claw v%s                 |\n", RT_CLAW_VERSION);
    claw_log_raw("  |  Real-Time Claw / Swarm Intelligence    |\n");
    claw_log_raw("  +-----------------------------------------+\n");
    claw_log_raw("\n");

    /* Phase 1: initialize all services */
    for (size_t i = 0; i < SERVICE_COUNT; i++) {
        CLAW_LOGI(TAG, "init: %s", s_services[i].name);
        int ret = s_services[i].init();
        if (ret != CLAW_OK) {
            CLAW_LOGW(TAG, "%s init returned %d", s_services[i].name, ret);
        }
    }

    /* Phase 2: start services that have a runtime phase */
    for (size_t i = 0; i < SERVICE_COUNT; i++) {
        if (s_services[i].start) {
            CLAW_LOGI(TAG, "start: %s", s_services[i].name);
            s_services[i].start();
        }
    }

    /*
     * AI boot test: skip when disabled via Kconfig, or when IM
     * channels are active (both compete for TLS memory).
     */
#ifdef CONFIG_RTCLAW_AI_BOOT_TEST
#if !defined(CONFIG_RTCLAW_FEISHU_ENABLE) && \
    !defined(CONFIG_RTCLAW_TELEGRAM_ENABLE)
    if (!claw_thread_create("ai_test", ai_boot_test_thread,
                            NULL, 8192, 20)) {
        CLAW_LOGW(TAG, "ai_test thread create failed");
    }
#endif
#endif

    return CLAW_OK;
}
