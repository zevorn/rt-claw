/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 *
 * System tools — chip info, memory, uptime, restart.
 */

#include "claw_tools.h"
#include "claw_config.h"

#include <stdio.h>

#define TAG "tool_sys"

#ifdef CLAW_PLATFORM_ESP_IDF

#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

static int tool_system_info(const cJSON *params, cJSON *result)
{
    (void)params;

    esp_chip_info_t ci;
    esp_chip_info(&ci);

    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddStringToObject(result, "project", "rt-claw");
    cJSON_AddStringToObject(result, "version", RT_CLAW_VERSION);
    cJSON_AddStringToObject(result, "chip", "ESP32-C3");
    cJSON_AddNumberToObject(result, "cores", ci.cores);
    cJSON_AddNumberToObject(result, "revision", ci.revision);

    int64_t uptime_us = esp_timer_get_time();
    cJSON_AddNumberToObject(result, "uptime_seconds",
                            (double)uptime_us / 1000000.0);

    return CLAW_OK;
}

static int tool_memory_info(const cJSON *params, cJSON *result)
{
    (void)params;

    size_t total = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    size_t free_sz = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    size_t min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);

    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddNumberToObject(result, "heap_total_bytes", total);
    cJSON_AddNumberToObject(result, "heap_free_bytes", free_sz);
    cJSON_AddNumberToObject(result, "heap_min_free_bytes", min_free);
    cJSON_AddNumberToObject(result, "heap_used_percent",
                            (double)(total - free_sz) / total * 100.0);

    return CLAW_OK;
}

static int tool_system_restart(const cJSON *params, cJSON *result)
{
    (void)params;

    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddStringToObject(result, "message",
                            "System will restart in 2 seconds");
    CLAW_LOGW(TAG, "restart requested via tool");

    /* Delay to let the response be sent back */
    claw_thread_delay_ms(2000);
    esp_restart();

    /* Never reached */
    return CLAW_OK;
}

static const char schema_empty[] =
    "{\"type\":\"object\",\"properties\":{}}";

void claw_tools_register_system(void)
{
    claw_tool_register("system_info",
        "Get system information: chip model, firmware version, "
        "core count, revision, and uptime in seconds.",
        schema_empty, tool_system_info);

    claw_tool_register("memory_info",
        "Get heap memory status: total, free, minimum-ever-free bytes, "
        "and usage percentage.",
        schema_empty, tool_memory_info);

    claw_tool_register("system_restart",
        "Restart the ESP32-C3 system. Use with caution — "
        "this will reboot the device after a 2-second delay.",
        schema_empty, tool_system_restart);
}

#else /* non-ESP-IDF */

void claw_tools_register_system(void)
{
    /* System tools not available on this platform */
}

#endif
