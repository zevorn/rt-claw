/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Tool framework — register hardware capabilities as LLM tools.
 */

#ifndef CLAW_TOOLS_H
#define CLAW_TOOLS_H

#include "osal/claw_os.h"
#include "cJSON.h"

#define CLAW_TOOL_MAX       16
#define CLAW_TOOL_NAME_MAX  32

/*
 * Tool execute function.
 * @param params  Input parameters (cJSON object, may be NULL)
 * @param result  Output: caller-allocated cJSON object to fill
 * @return CLAW_OK on success
 */
typedef int (*claw_tool_fn)(const cJSON *params, cJSON *result);

typedef struct {
    char name[CLAW_TOOL_NAME_MAX];
    const char *description;
    const char *input_schema_json;  /* JSON string of input_schema */
    claw_tool_fn execute;
} claw_tool_t;

/**
 * Initialize tool registry. Call once at startup.
 */
int claw_tools_init(void);

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

/**
 * Build cJSON array of all tool definitions for Claude API.
 * Caller must cJSON_Delete() the result.
 */
cJSON *claw_tools_to_json(void);

/**
 * Build cJSON array of tool definitions, excluding names that start
 * with the given prefix.  Used by background AI calls (scheduled tasks,
 * skills) to omit slow I/O tools like lcd_*.
 * Caller must cJSON_Delete() the result.
 */
cJSON *claw_tools_to_json_exclude(const char *prefix);

/**
 * Register built-in GPIO tools.
 */
void claw_tools_register_gpio(void);

/**
 * Register built-in system tools.
 */
void claw_tools_register_system(void);

/**
 * Register built-in LCD tools.
 */
void claw_tools_register_lcd(void);

/**
 * Register scheduler tools (schedule_task, remove_task).
 */
void claw_tools_register_sched(void);

/**
 * Scheduled-task reply callback.
 * @param target  Opaque destination (e.g. Feishu chat_id)
 * @param text    Reply text to deliver
 */
typedef void (*sched_reply_fn_t)(const char *target, const char *text);

/**
 * Set the reply context for the NEXT scheduled task registration.
 * Call before ai_chat(); tool_schedule_task() captures the context
 * so that subsequent scheduled firings route replies accordingly.
 * Pass NULL to reset (replies go to serial console).
 *
 * Thread-safety: protected by an internal mutex.
 */
void sched_set_reply_context(sched_reply_fn_t fn, const char *target);

/**
 * Register network tools (http_request).
 */
void claw_tools_register_net(void);

/**
 * Initialize LCD panel (QEMU RGB framebuffer).
 * Call before claw_tools_init() so the panel is ready.
 */
int claw_lcd_init(void);

/**
 * Check whether LCD hardware was initialized successfully.
 */
int claw_lcd_available(void);

/**
 * Show status message on LCD bottom area.
 */
void claw_lcd_status(const char *msg);

/**
 * Draw progress bar on LCD bottom strip (0-100%).
 */
void claw_lcd_progress(int percent);

#endif /* CLAW_TOOLS_H */
