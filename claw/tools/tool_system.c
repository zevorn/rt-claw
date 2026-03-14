/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * System tools — chip info, memory, uptime, restart.
 */

#include "claw/tools/claw_tools.h"
#include "claw/claw_config.h"
#include "claw/services/ai/ai_memory.h"

#include <stdio.h>

#define TAG "tool_sys"

/* ---- Long-term memory tools (platform-independent) ---- */

static int tool_save_memory(const cJSON *params, cJSON *result)
{
    cJSON *key_j = cJSON_GetObjectItem(params, "key");
    cJSON *val_j = cJSON_GetObjectItem(params, "value");

    if (!key_j || !cJSON_IsString(key_j) ||
        !val_j || !cJSON_IsString(val_j)) {
        cJSON_AddStringToObject(result, "error",
                                "missing key or value");
        return CLAW_ERROR;
    }

    if (ai_ltm_save(key_j->valuestring,
                     val_j->valuestring) != CLAW_OK) {
        cJSON_AddStringToObject(result, "error",
                                "memory full or save failed");
        return CLAW_ERROR;
    }

    cJSON_AddStringToObject(result, "status", "ok");
    char msg[96];
    snprintf(msg, sizeof(msg), "saved: %s = %s",
             key_j->valuestring, val_j->valuestring);
    cJSON_AddStringToObject(result, "message", msg);
    return CLAW_OK;
}

static int tool_delete_memory(const cJSON *params, cJSON *result)
{
    cJSON *key_j = cJSON_GetObjectItem(params, "key");

    if (!key_j || !cJSON_IsString(key_j)) {
        cJSON_AddStringToObject(result, "error", "missing key");
        return CLAW_ERROR;
    }

    if (ai_ltm_delete(key_j->valuestring) != CLAW_OK) {
        cJSON_AddStringToObject(result, "error", "key not found");
        return CLAW_ERROR;
    }

    cJSON_AddStringToObject(result, "status", "ok");
    char msg[64];
    snprintf(msg, sizeof(msg), "deleted: %s", key_j->valuestring);
    cJSON_AddStringToObject(result, "message", msg);
    return CLAW_OK;
}

static int tool_list_memories(const cJSON *params, cJSON *result)
{
    (void)params;
    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddNumberToObject(result, "count", ai_ltm_count());

    char *ctx = ai_ltm_build_context();
    if (ctx) {
        cJSON_AddStringToObject(result, "memories", ctx);
        claw_free(ctx);
    } else {
        cJSON_AddStringToObject(result, "memories", "(empty)");
    }
    return CLAW_OK;
}

static const char schema_save_memory[] =
    "{\"type\":\"object\","
    "\"properties\":{"
    "\"key\":{\"type\":\"string\","
    "\"description\":\"Memory key (e.g. user_name, preference)\"},"
    "\"value\":{\"type\":\"string\","
    "\"description\":\"Value to remember\"}},"
    "\"required\":[\"key\",\"value\"]}";

static const char schema_delete_memory[] =
    "{\"type\":\"object\","
    "\"properties\":{"
    "\"key\":{\"type\":\"string\","
    "\"description\":\"Memory key to delete\"}},"
    "\"required\":[\"key\"]}";

static void register_memory_tools(void)
{
    static const char se[] =
        "{\"type\":\"object\",\"properties\":{}}";

    claw_tool_register("save_memory",
        "Save a fact to long-term memory (persists across reboots). "
        "Use when the user asks you to remember something: their "
        "name, preferences, important facts, nicknames, etc.",
        schema_save_memory, tool_save_memory);

    claw_tool_register("delete_memory",
        "Delete a fact from long-term memory by key.",
        schema_delete_memory, tool_delete_memory);

    claw_tool_register("list_memories",
        "List all facts stored in long-term memory.",
        se, tool_list_memories);
}

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
    const char *chip_name;
    switch (ci.model) {
    case CHIP_ESP32:
        chip_name = "ESP32";
        break;
    case CHIP_ESP32S2:
        chip_name = "ESP32-S2";
        break;
    case CHIP_ESP32S3:
        chip_name = "ESP32-S3";
        break;
    case CHIP_ESP32C3:
        chip_name = "ESP32-C3";
        break;
    case CHIP_ESP32H2:
        chip_name = "ESP32-H2";
        break;
    case CHIP_ESP32C6:
        chip_name = "ESP32-C6";
        break;
    default:
        chip_name = "Unknown";
        break;
    }
    cJSON_AddStringToObject(result, "chip", chip_name);
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

static int tool_clear_history(const cJSON *params, cJSON *result)
{
    (void)params;
    int count = ai_memory_count();
    ai_memory_clear();
    cJSON_AddStringToObject(result, "status", "ok");
    char msg[64];
    snprintf(msg, sizeof(msg),
             "cleared %d messages from conversation history", count);
    cJSON_AddStringToObject(result, "message", msg);
    cJSON_AddNumberToObject(result, "freed_messages", count);
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

    claw_tool_register("clear_history",
        "Clear the conversation history to free memory. "
        "Use when memory is low or the conversation is too long.",
        schema_empty, tool_clear_history);

    register_memory_tools();

    claw_tool_register("system_restart",
        "Restart the system. Use with caution — "
        "this will reboot the device after a 2-second delay.",
        schema_empty, tool_system_restart);
}

#elif defined(CLAW_PLATFORM_RTTHREAD)

#include <rtthread.h>

static int tool_system_info(const cJSON *params, cJSON *result)
{
    (void)params;

    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddStringToObject(result, "project", "rt-claw");
    cJSON_AddStringToObject(result, "version", RT_CLAW_VERSION);
    cJSON_AddStringToObject(result, "platform", "QEMU vexpress-a9");
    cJSON_AddStringToObject(result, "rtos", "RT-Thread");
    cJSON_AddNumberToObject(result, "rtos_version", RT_VERSION_MAJOR * 10000
                            + RT_VERSION_MINOR * 100 + RT_VERSION_PATCH);
    cJSON_AddNumberToObject(result, "uptime_seconds",
                            (double)rt_tick_get() / RT_TICK_PER_SECOND);

    return CLAW_OK;
}

static int tool_memory_info(const cJSON *params, cJSON *result)
{
    (void)params;

    rt_size_t total = 0, used = 0, max_used = 0;
    rt_memory_info(&total, &used, &max_used);

    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddNumberToObject(result, "heap_total_bytes", (double)total);
    cJSON_AddNumberToObject(result, "heap_free_bytes",
                            (double)(total - used));
    cJSON_AddNumberToObject(result, "heap_max_used_bytes", (double)max_used);
    if (total > 0) {
        cJSON_AddNumberToObject(result, "heap_used_percent",
                                (double)used / total * 100.0);
    }

    return CLAW_OK;
}

static const char schema_empty[] =
    "{\"type\":\"object\",\"properties\":{}}";

static int tool_clear_history(const cJSON *params, cJSON *result)
{
    (void)params;
    int count = ai_memory_count();
    ai_memory_clear();
    cJSON_AddStringToObject(result, "status", "ok");
    char msg[64];
    snprintf(msg, sizeof(msg),
             "cleared %d messages from conversation history", count);
    cJSON_AddStringToObject(result, "message", msg);
    return CLAW_OK;
}

void claw_tools_register_system(void)
{
    claw_tool_register("system_info",
        "Get system information: platform, RTOS version, and uptime.",
        schema_empty, tool_system_info);

    claw_tool_register("memory_info",
        "Get heap memory status: total, free, max-ever-used bytes, "
        "and usage percentage.",
        schema_empty, tool_memory_info);

    claw_tool_register("clear_history",
        "Clear the conversation history to free memory.",
        schema_empty, tool_clear_history);

    register_memory_tools();
}

#else /* unknown platform */

void claw_tools_register_system(void)
{
    /* System tools not available on this platform */
}

#endif
