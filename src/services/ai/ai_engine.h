/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 */

#ifndef __CLAW_AI_ENGINE_H__
#define __CLAW_AI_ENGINE_H__

#include "claw_os.h"

int ai_engine_init(void);

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

#endif /* __CLAW_AI_ENGINE_H__ */
