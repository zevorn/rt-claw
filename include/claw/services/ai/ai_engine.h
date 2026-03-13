/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef CLAW_SERVICES_AI_ENGINE_H
#define CLAW_SERVICES_AI_ENGINE_H

#include "osal/claw_os.h"

/* Status phases for the progress callback */
#define AI_STATUS_THINKING   0  /* waiting for API response */
#define AI_STATUS_TOOL_CALL  1  /* executing a tool (detail = name) */
#define AI_STATUS_DONE       2  /* request complete */

typedef void (*ai_status_cb_t)(int status, const char *detail);

void ai_set_status_cb(ai_status_cb_t cb);

int ai_engine_init(void);

/**
 * Runtime configuration — update API credentials without recompiling.
 * New values take effect on the next ai_chat() call.
 * Caller is responsible for NVS persistence (platform-specific).
 */
void ai_set_api_key(const char *key);
void ai_set_api_url(const char *url);
void ai_set_model(const char *model);

const char *ai_get_api_key(void);
const char *ai_get_api_url(void);
const char *ai_get_model(void);

/**
 * Set channel hint appended to the system prompt.
 * Call before ai_chat() to tell the model which channel is active
 * (e.g. "Feishu IM", "serial console").  Pass NULL to clear.
 * The string is copied internally; caller may free after return.
 */
void ai_set_channel_hint(const char *hint);

/**
 * Send a user message to the LLM and receive a reply.
 * Stores user/assistant messages in conversation memory.
 */
int ai_chat(const char *user_msg, char *reply, size_t reply_size);

/**
 * One-shot LLM call without conversation memory.
 * Used by skill system to avoid polluting main history.
 */
int ai_chat_raw(const char *prompt, char *reply, size_t reply_size);

#endif /* CLAW_SERVICES_AI_ENGINE_H */
