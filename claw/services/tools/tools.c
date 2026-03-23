/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Tool registry — thin wrapper over tool_core OOP list.
 * Tools self-register via CLAW_TOOL_REGISTER(); this file provides
 * lookup, counting, and JSON export for the AI / shell layer.
 */

#include "claw/services/tools/tools.h"
#include "claw_config.h"

#include <string.h>

#define TAG "tools"

int claw_tools_init(void)
{
    /*
     * Remove LCD tools if LCD hardware is not available.
     * claw_lcd_init() runs before services start, so
     * claw_lcd_available() is authoritative by now.
     */
#ifdef CONFIG_RTCLAW_TOOL_LCD
    if (!claw_lcd_available()) {
        claw_list_node_t *head = claw_tool_core_list();
        claw_list_node_t *pos;
        claw_list_node_t *tmp;

        claw_list_for_each_safe(pos, tmp, head) {
            struct claw_tool *t =
                claw_list_entry(pos, struct claw_tool, node);
            if (strncmp(t->name, "lcd_", 4) == 0) {
                claw_list_del(&t->node);
            }
        }
    }
#endif

    claw_err_t err = claw_tool_core_init_all();
    if (err != CLAW_OK) {
        CLAW_LOGE(TAG, "tool init failed: %s", claw_strerror(err));
        return (int)err;
    }

    CLAW_LOGI(TAG, "%d tools registered", claw_tool_core_count());
    return CLAW_OK;
}

void claw_tools_stop(void)
{
    claw_tool_core_cleanup_all();
}

int claw_tools_count(void)
{
    return claw_tool_core_count();
}

const struct claw_tool *claw_tool_find(const char *name)
{
    return claw_tool_core_find(name);
}

cJSON *claw_tools_to_json(void)
{
    return claw_tools_to_json_exclude(NULL);
}

cJSON *claw_tools_to_json_exclude(const char *prefix)
{
    cJSON *arr = cJSON_CreateArray();
    if (!arr) {
        return NULL;
    }

    size_t prefix_len = prefix ? strlen(prefix) : 0;
    claw_list_node_t *head = claw_tool_core_list();
    claw_list_node_t *pos;

    claw_list_for_each(pos, head) {
        const struct claw_tool *t =
            claw_list_entry(pos, struct claw_tool, node);

        if (prefix_len > 0 &&
            strncmp(t->name, prefix, prefix_len) == 0) {
            continue;
        }

        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "name", t->name);
        cJSON_AddStringToObject(tool, "description", t->description);

        cJSON *schema = cJSON_Parse(t->input_schema_json);
        if (schema) {
            cJSON_AddItemToObject(tool, "input_schema", schema);
        }
        cJSON_AddItemToArray(arr, tool);
    }
    return arr;
}

/*
 * Weak stubs for scheduler tool functions.
 * Overridden by services/tools/sched.c when tool_sched is enabled.
 */
__attribute__((weak))
void sched_set_reply_context(sched_reply_fn_t fn, const char *target)
{
    (void)fn;
    (void)target;
}

__attribute__((weak))
int sched_tool_remove_by_name(const char *name)
{
    (void)name;
    return -1;
}

/* OOP service registration */
#include "claw/core/service.h"
#ifdef CONFIG_RTCLAW_SCHED_ENABLE
static const char *tools_deps[] = { "ai_engine", "sched", NULL };
#else
static const char *tools_deps[] = { "ai_engine", NULL };
#endif
CLAW_DEFINE_SIMPLE_SERVICE(tools, "tools",
    claw_tools_init, NULL, claw_tools_stop, tools_deps);
