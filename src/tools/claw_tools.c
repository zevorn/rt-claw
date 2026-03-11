/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 */

#include "claw_tools.h"

#include <string.h>

#define TAG "tools"

static claw_tool_t s_tools[CLAW_TOOL_MAX];
static int s_tool_count;

void claw_tools_init(void)
{
    memset(s_tools, 0, sizeof(s_tools));
    s_tool_count = 0;

    claw_tools_register_gpio();
    claw_tools_register_system();

    CLAW_LOGI(TAG, "%d tools registered", s_tool_count);
}

int claw_tool_register(const char *name, const char *description,
                       const char *input_schema_json,
                       claw_tool_fn execute)
{
    if (s_tool_count >= CLAW_TOOL_MAX) {
        CLAW_LOGE(TAG, "tool registry full");
        return CLAW_ERROR;
    }
    if (!name || !execute) {
        return CLAW_ERROR;
    }

    claw_tool_t *t = &s_tools[s_tool_count];
    strncpy(t->name, name, CLAW_TOOL_NAME_MAX - 1);
    t->name[CLAW_TOOL_NAME_MAX - 1] = '\0';
    t->description = description;
    t->input_schema_json = input_schema_json;
    t->execute = execute;
    s_tool_count++;

    CLAW_LOGD(TAG, "registered: %s", name);
    return CLAW_OK;
}

int claw_tools_count(void)
{
    return s_tool_count;
}

const claw_tool_t *claw_tool_get(int index)
{
    if (index < 0 || index >= s_tool_count) {
        return NULL;
    }
    return &s_tools[index];
}

const claw_tool_t *claw_tool_find(const char *name)
{
    for (int i = 0; i < s_tool_count; i++) {
        if (strcmp(s_tools[i].name, name) == 0) {
            return &s_tools[i];
        }
    }
    return NULL;
}

#ifdef CLAW_PLATFORM_ESP_IDF

cJSON *claw_tools_to_json(void)
{
    cJSON *arr = cJSON_CreateArray();
    if (!arr) {
        return NULL;
    }

    for (int i = 0; i < s_tool_count; i++) {
        const claw_tool_t *t = &s_tools[i];
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

#endif
