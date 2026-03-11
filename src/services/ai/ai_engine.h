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
 * @param user_msg  User input text (null-terminated)
 * @param reply     Buffer to store LLM reply
 * @param reply_size Size of reply buffer
 * @return CLAW_OK on success, CLAW_ERROR on failure
 */
int ai_chat(const char *user_msg, char *reply, size_t reply_size);

#endif /* __CLAW_AI_ENGINE_H__ */
