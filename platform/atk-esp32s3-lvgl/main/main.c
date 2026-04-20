/*
 * Copyright (c) 2026, Tang Sun <714858993@qq.com>
 * SPDX-License-Identifier: MIT
 *
 * Platform entry for ESP32-S3 / ESP-IDF + FreeRTOS.
 */

#include "osal/claw_os.h"
#include "osal/claw_kv.h"
#include "claw/init.h"
#include "claw/shell/shell_commands.h"
#include "platform/board.h"

#ifdef CLAW_PLATFORM_ESP_IDF
#include "esp_log.h"
#endif

#ifdef CONFIG_RTCLAW_SHELL_ENABLE
#include "platform/common/espressif/esp_shell.h"
#endif

void app_main(void)
{
#ifdef CLAW_PLATFORM_ESP_IDF
    claw_kv_init();

#ifdef CONFIG_RTCLAW_SHELL_ENABLE
    esp_log_level_set("*", ESP_LOG_WARN);
#else
    claw_log_set_enabled(1);
#endif
#endif

    board_early_init();
    shell_nvs_config_load();
    claw_init();

#ifdef CONFIG_RTCLAW_SHELL_ENABLE
    esp_shell_loop();
#endif

    while (1) {
        claw_thread_delay_ms(1000);
    }
}
