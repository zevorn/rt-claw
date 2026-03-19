/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "osal/claw_os.h"
#include "claw_config.h"
#include "claw/claw_init.h"
#include "claw/core/claw_service.h"
#include "claw/core/claw_driver.h"
#include "claw/core/claw_tool.h"
#include "claw/services/ai/ai_engine.h"

#include <stdio.h>

#define TAG "init"

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

    /*
     * Collect drivers and services from linker sections,
     * then probe drivers and start services in dependency order.
     */
    claw_driver_collect_from_section();
    claw_driver_probe_all();

    claw_tool_core_collect_from_section();

    claw_service_collect_from_section();
    claw_err_t err = claw_service_start_all();
    if (err != CLAW_OK) {
        CLAW_LOGE(TAG, "service startup failed: %s", claw_strerror(err));
        return (int)err;
    }

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

void claw_deinit(void)
{
    CLAW_LOGI(TAG, "shutting down ...");
    claw_service_stop_all();
    claw_driver_remove_all();
    CLAW_LOGI(TAG, "all services stopped");
}
