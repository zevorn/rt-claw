/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 *
 * Tool framework — register hardware capabilities as LLM tools.
 */

#ifndef __CLAW_TOOLS_H__
#define __CLAW_TOOLS_H__

#include "claw_os.h"

#ifdef CLAW_PLATFORM_ESP_IDF
#include "cJSON.h"
#endif

#define CLAW_TOOL_MAX       16
#define CLAW_TOOL_NAME_MAX  32

/*
 * Tool execute function.
 * @param params  Input parameters (cJSON object, may be NULL)
 * @param result  Output: caller-allocated cJSON object to fill
 * @return CLAW_OK on success
 */
#ifdef CLAW_PLATFORM_ESP_IDF
typedef int (*claw_tool_fn)(const cJSON *params, cJSON *result);
#else
typedef int (*claw_tool_fn)(const void *params, void *result);
#endif

typedef struct {
    char name[CLAW_TOOL_NAME_MAX];
    const char *description;
    const char *input_schema_json;  /* JSON string of input_schema */
    claw_tool_fn execute;
} claw_tool_t;

/**
 * Initialize tool registry. Call once at startup.
 */
void claw_tools_init(void);

/**
 * Register a tool. Returns CLAW_OK or CLAW_ERROR if full.
 */
int claw_tool_register(const char *name, const char *description,
                       const char *input_schema_json,
                       claw_tool_fn execute);

/**
 * Get number of registered tools.
 */
int claw_tools_count(void);

/**
 * Get tool by index (0-based). Returns NULL if out of range.
 */
const claw_tool_t *claw_tool_get(int index);

/**
 * Find tool by name. Returns NULL if not found.
 */
const claw_tool_t *claw_tool_find(const char *name);

#ifdef CLAW_PLATFORM_ESP_IDF
/**
 * Build cJSON array of all tool definitions for Claude API.
 * Caller must cJSON_Delete() the result.
 */
cJSON *claw_tools_to_json(void);
#endif

/**
 * Register built-in GPIO tools.
 */
void claw_tools_register_gpio(void);

/**
 * Register built-in system tools.
 */
void claw_tools_register_system(void);

#endif /* __CLAW_TOOLS_H__ */
