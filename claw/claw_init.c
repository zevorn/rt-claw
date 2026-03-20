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
#include "claw/tools/claw_tools.h"
#include "claw/services/ai/ai_engine.h"

#include <stdio.h>

#define TAG "init"

#ifdef CONFIG_RTCLAW_AI_BOOT_TEST
#if !defined(CONFIG_RTCLAW_FEISHU_ENABLE) && \
    !defined(CONFIG_RTCLAW_TELEGRAM_ENABLE)
static struct claw_thread *s_ai_test_thread;
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
    /* rt = cyan, claw = amber, split at column 11 */
    claw_log_raw(
        "\033[36m" "         __  "
        "\033[33m" "        __\n"
        "\033[36m" "   _____/ /_ "
        "\033[33m" " ______/ /___ __     __\n"
        "\033[36m" "  / ___/ __/ "
        "\033[33m" "/ ___/ / __ `/ | /| / /\n"
        "\033[36m" " / /  / /_  "
        "\033[33m" "/ /__/ / /_/ /| |/ |/ /\n"
        "\033[36m" "/_/   \\__/  "
        "\033[33m" "\\___/_/\\__,_/ |__/|__/\n"
        "\033[0m\n");
    claw_log_raw("  RT-Claw v%s\n", RT_CLAW_VERSION);
    claw_log_raw("\n");

    /*
     * Collect drivers, tools, and services from linker sections.
     * On ESP-IDF, __attribute__((constructor)) already called
     * the register functions before main — skip section scan.
     * Each collect function has its own idempotency guard.
     */
#ifndef CLAW_PLATFORM_ESP_IDF
    claw_driver_collect_from_section();
    claw_tool_core_collect_from_section();
    claw_service_collect_from_section();
#endif

    claw_driver_probe_all();

#ifdef CONFIG_RTCLAW_LCD_ENABLE
    claw_lcd_init();
#endif

    claw_err_t err = claw_service_start_all();
    if (err != CLAW_OK) {
        CLAW_LOGE(TAG, "service startup failed: %s", claw_strerror(err));
        return (int)err;
    }

#ifdef CONFIG_RTCLAW_AI_BOOT_TEST
#if !defined(CONFIG_RTCLAW_FEISHU_ENABLE) && \
    !defined(CONFIG_RTCLAW_TELEGRAM_ENABLE)
    s_ai_test_thread = claw_thread_create("ai_test",
        ai_boot_test_thread, NULL, 8192, 20);
    if (!s_ai_test_thread) {
        CLAW_LOGW(TAG, "ai_test thread create failed");
    }
#endif
#endif

    return CLAW_OK;
}

void claw_deinit(void)
{
    CLAW_LOGI(TAG, "shutting down ...");

#ifdef CONFIG_RTCLAW_AI_BOOT_TEST
#if !defined(CONFIG_RTCLAW_FEISHU_ENABLE) && \
    !defined(CONFIG_RTCLAW_TELEGRAM_ENABLE)
    if (s_ai_test_thread) {
        claw_thread_delete(s_ai_test_thread);
        s_ai_test_thread = NULL;
    }
#endif
#endif

    claw_service_stop_all();
    claw_driver_remove_all();
    CLAW_LOGI(TAG, "all services stopped");
}
