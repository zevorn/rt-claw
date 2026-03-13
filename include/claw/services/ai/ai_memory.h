/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * AI memory — short-term (RAM) and long-term (Flash) storage.
 */

#ifndef CLAW_AI_MEMORY_H
#define CLAW_AI_MEMORY_H

#include "osal/claw_os.h"
#include "cJSON.h"

#ifdef CLAW_PLATFORM_ESP_IDF
#include "sdkconfig.h"
#endif

/* ---- Short-term memory (RAM ring buffer, conversation turns) ---- */

int   ai_memory_init(void);
void  ai_memory_add_message(const char *role, const char *content_json);
cJSON *ai_memory_build_messages(void);  /* caller frees */
void  ai_memory_clear(void);
int   ai_memory_count(void);

/* ---- Long-term memory (NVS Flash, persistent facts) ---- */

#define LTM_MAX_ENTRIES     16
#define LTM_KEY_MAX         32
#define LTM_VALUE_MAX       128

int   ai_ltm_init(void);
int   ai_ltm_save(const char *key, const char *value);
int   ai_ltm_load(const char *key, char *value, size_t size);
int   ai_ltm_delete(const char *key);
void  ai_ltm_list(void);
int   ai_ltm_count(void);

/**
 * Build system prompt suffix from long-term memories.
 * Returns heap-allocated string, caller frees. NULL if empty.
 */
char *ai_ltm_build_context(void);

#endif /* CLAW_AI_MEMORY_H */
