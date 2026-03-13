/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * AI skill — predefined prompt templates.
 */

#ifndef CLAW_AI_SKILL_H
#define CLAW_AI_SKILL_H

#include "osal/claw_os.h"

#define SKILL_MAX       8
#define SKILL_NAME_MAX  24

int  ai_skill_init(void);
int  ai_skill_register(const char *name, const char *desc,
                       const char *prompt_template);
int  ai_skill_execute(const char *name, const char *params,
                      char *reply, size_t reply_size);
const char *ai_skill_find(const char *name);
void ai_skill_list(void);

#endif /* CLAW_AI_SKILL_H */
