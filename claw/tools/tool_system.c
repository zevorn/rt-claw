/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * System tools — chip info, memory, uptime, restart.
 */

#include "claw/tools/claw_tools.h"
#include "claw_config.h"
#include "claw/services/ai/ai_memory.h"

#include <stdio.h>

#define TAG "tool_sys"

/* ---- Long-term memory tools (platform-independent) ---- */

static claw_err_t tool_save_memory(struct claw_tool *tool,
                                   const cJSON *params, cJSON *result)
{
    (void)tool;
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

static claw_err_t tool_delete_memory(struct claw_tool *tool,
                                     const cJSON *params, cJSON *result)
{
    (void)tool;
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

static claw_err_t tool_list_memories(struct claw_tool *tool,
                                     const cJSON *params, cJSON *result)
{
    (void)tool;
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

static const char schema_empty[] =
    "{\"type\":\"object\",\"properties\":{}}";

#ifdef CLAW_PLATFORM_ESP_IDF

#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_timer.h"

static claw_err_t tool_system_info(struct claw_tool *tool,
                                   const cJSON *params, cJSON *result)
{
    (void)tool;
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

    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str),
                 "%02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        cJSON_AddStringToObject(result, "mac", mac_str);
    }

    int64_t uptime_us = esp_timer_get_time();
    cJSON_AddNumberToObject(result, "uptime_seconds",
                            (double)uptime_us / 1000000.0);

    size_t free_heap = esp_get_free_heap_size();
    size_t min_heap = esp_get_minimum_free_heap_size();
    cJSON_AddNumberToObject(result, "free_heap", (double)free_heap);
    cJSON_AddNumberToObject(result, "min_free_heap", (double)min_heap);

    return CLAW_OK;
}

static claw_err_t tool_memory_info(struct claw_tool *tool,
                                   const cJSON *params, cJSON *result)
{
    (void)tool;
    (void)params;

    size_t total = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    size_t free_sz = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    size_t min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddNumberToObject(result, "heap_total_bytes", total);
    cJSON_AddNumberToObject(result, "heap_free_bytes", free_sz);
    cJSON_AddNumberToObject(result, "heap_min_free_bytes", min_free);
    cJSON_AddNumberToObject(result, "heap_largest_block", largest);
    cJSON_AddNumberToObject(result, "heap_used_percent",
                            (double)(total - free_sz) / total * 100.0);

    /* fragmentation: 100% = all fragments, 0% = single block */
    if (free_sz > 0) {
        double frag = (1.0 - (double)largest / free_sz) * 100.0;
        cJSON_AddNumberToObject(result, "fragmentation_percent", frag);
    }

    return CLAW_OK;
}

static claw_err_t tool_system_restart(struct claw_tool *tool,
                                      const cJSON *params, cJSON *result)
{
    (void)tool;
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

static claw_err_t tool_clear_history(struct claw_tool *tool,
                                     const cJSON *params, cJSON *result)
{
    (void)tool;
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

#elif defined(CLAW_PLATFORM_RTTHREAD)

#include <rtthread.h>

static claw_err_t tool_system_info(struct claw_tool *tool,
                                   const cJSON *params, cJSON *result)
{
    (void)tool;
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

static claw_err_t tool_memory_info(struct claw_tool *tool,
                                   const cJSON *params, cJSON *result)
{
    (void)tool;
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

static claw_err_t tool_clear_history(struct claw_tool *tool,
                                     const cJSON *params, cJSON *result)
{
    (void)tool;
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

/* system_restart not available on RT-Thread */
static claw_err_t tool_system_restart(struct claw_tool *tool,
                                      const cJSON *params, cJSON *result)
{
    (void)tool;
    (void)params;
    cJSON_AddStringToObject(result, "error",
                            "restart not supported on this platform");
    return CLAW_OK;
}

#elif defined(CLAW_PLATFORM_LINUX)

#include <stdlib.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>

static claw_err_t tool_system_info(struct claw_tool *tool,
                                   const cJSON *params, cJSON *result)
{
    (void)tool;
    (void)params;
    struct utsname uts;
    uname(&uts);

    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddStringToObject(result, "project", "rt-claw");
    cJSON_AddStringToObject(result, "version", RT_CLAW_VERSION);
    cJSON_AddStringToObject(result, "platform", "Linux native");
    cJSON_AddStringToObject(result, "kernel", uts.release);
    cJSON_AddStringToObject(result, "arch", uts.machine);

    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        cJSON_AddNumberToObject(result, "uptime_seconds",
                                (double)si.uptime);
    }
    return CLAW_OK;
}

static claw_err_t tool_memory_info(struct claw_tool *tool,
                                   const cJSON *params, cJSON *result)
{
    (void)tool;
    (void)params;
    struct sysinfo si;
    if (sysinfo(&si) != 0) {
        cJSON_AddStringToObject(result, "error",
                                "sysinfo failed");
        return CLAW_ERROR;
    }

    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddNumberToObject(result, "total_ram_mb",
        (double)si.totalram * si.mem_unit / (1024 * 1024));
    cJSON_AddNumberToObject(result, "free_ram_mb",
        (double)si.freeram * si.mem_unit / (1024 * 1024));
    cJSON_AddNumberToObject(result, "used_percent",
        (1.0 - (double)si.freeram / si.totalram) * 100.0);
    return CLAW_OK;
}

static claw_err_t tool_clear_history(struct claw_tool *tool,
                                     const cJSON *params, cJSON *result)
{
    (void)tool;
    (void)params;
    int count = ai_memory_count();
    ai_memory_clear();
    cJSON_AddStringToObject(result, "status", "ok");
    char msg[64];
    snprintf(msg, sizeof(msg),
             "cleared %d messages", count);
    cJSON_AddStringToObject(result, "message", msg);
    return CLAW_OK;
}

static claw_err_t tool_system_restart(struct claw_tool *tool,
                                      const cJSON *params, cJSON *result)
{
    (void)tool;
    (void)params;
    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddStringToObject(result, "message",
                            "Exiting (Linux native)");
    CLAW_LOGW(TAG, "exit requested via tool");
    claw_thread_delay_ms(500);
    exit(0);
    return CLAW_OK;
}

#else /* unknown platform */

static claw_err_t tool_system_info(struct claw_tool *tool,
                                   const cJSON *params, cJSON *result)
{
    (void)tool;
    (void)params;
    cJSON_AddStringToObject(result, "error", "not supported");
    return CLAW_OK;
}
#define tool_memory_info    tool_system_info
#define tool_clear_history  tool_system_info
#define tool_system_restart tool_system_info

#endif

/* ---- OOP tool registration ---- */

#ifdef CONFIG_RTCLAW_TOOL_SYSTEM

static const struct claw_tool_ops sys_info_ops = {
    .execute = tool_system_info,
};
static struct claw_tool sys_info_tool = {
    .name = "system_info",
    .description =
        "Get system information: chip model, firmware version, "
        "core count, revision, and uptime in seconds.",
    .input_schema_json = schema_empty,
    .ops = &sys_info_ops,
    .flags = CLAW_TOOL_LOCAL_ONLY,
};
CLAW_TOOL_REGISTER(system_info, &sys_info_tool);

static const struct claw_tool_ops mem_info_ops = {
    .execute = tool_memory_info,
};
static struct claw_tool mem_info_tool = {
    .name = "memory_info",
    .description =
        "Get heap memory status: total, free, minimum-ever-free, "
        "largest contiguous block, usage and fragmentation percent.",
    .input_schema_json = schema_empty,
    .ops = &mem_info_ops,
    .flags = CLAW_TOOL_LOCAL_ONLY,
};
CLAW_TOOL_REGISTER(memory_info, &mem_info_tool);

static const struct claw_tool_ops clear_hist_ops = {
    .execute = tool_clear_history,
};
static struct claw_tool clear_hist_tool = {
    .name = "clear_history",
    .description =
        "Clear the conversation history to free memory. "
        "Use when memory is low or the conversation is too long.",
    .input_schema_json = schema_empty,
    .ops = &clear_hist_ops,
    .flags = CLAW_TOOL_LOCAL_ONLY,
};
CLAW_TOOL_REGISTER(clear_history, &clear_hist_tool);

static const struct claw_tool_ops save_mem_ops = {
    .execute = tool_save_memory,
};
static struct claw_tool save_mem_tool = {
    .name = "save_memory",
    .description =
        "Save a fact to long-term memory (persists across reboots). "
        "Use when the user asks you to remember something: their "
        "name, preferences, important facts, nicknames, etc.",
    .input_schema_json = schema_save_memory,
    .ops = &save_mem_ops,
    .flags = CLAW_TOOL_LOCAL_ONLY,
};
CLAW_TOOL_REGISTER(save_memory, &save_mem_tool);

static const struct claw_tool_ops del_mem_ops = {
    .execute = tool_delete_memory,
};
static struct claw_tool del_mem_tool = {
    .name = "delete_memory",
    .description = "Delete a fact from long-term memory by key.",
    .input_schema_json = schema_delete_memory,
    .ops = &del_mem_ops,
    .flags = CLAW_TOOL_LOCAL_ONLY,
};
CLAW_TOOL_REGISTER(delete_memory, &del_mem_tool);

static const struct claw_tool_ops list_mem_ops = {
    .execute = tool_list_memories,
};
static struct claw_tool list_mem_tool = {
    .name = "list_memories",
    .description = "List all facts stored in long-term memory.",
    .input_schema_json = schema_empty,
    .ops = &list_mem_ops,
    .flags = CLAW_TOOL_LOCAL_ONLY,
};
CLAW_TOOL_REGISTER(list_memories, &list_mem_tool);

static const struct claw_tool_ops restart_ops = {
    .execute = tool_system_restart,
};
static struct claw_tool restart_tool = {
    .name = "system_restart",
    .description =
        "Restart the system. Use with caution — "
        "this will reboot the device after a 2-second delay.",
    .input_schema_json = schema_empty,
    .ops = &restart_ops,
    .flags = CLAW_TOOL_LOCAL_ONLY,
};
CLAW_TOOL_REGISTER(system_restart, &restart_tool);

#endif /* CONFIG_RTCLAW_TOOL_SYSTEM */
