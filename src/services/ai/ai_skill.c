/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 *
 * AI skill — predefined prompt templates + built-in skills.
 */

#include "claw_os.h"
#include "ai_skill.h"
#include "ai_engine.h"
#include "claw_tools.h"

#include <string.h>
#include <stdio.h>

#define TAG "ai_skill"
#define PROMPT_BUF_SIZE 512

typedef struct {
    char        name[SKILL_NAME_MAX];
    const char *description;
    const char *prompt_template;    /* %s = user params */
} ai_skill_t;

static ai_skill_t s_skills[SKILL_MAX];
static int        s_count;

static void register_builtins(void)
{
#ifdef CONFIG_CLAW_TOOL_LCD
    if (claw_lcd_available()) {
        ai_skill_register("draw",
            "Draw on LCD display",
            "Draw the following on the LCD display using lcd_* tools: %s");
    }
#endif

#ifdef CONFIG_CLAW_TOOL_SYSTEM
    ai_skill_register("monitor",
        "Check system health",
        "Check system health via system_info and memory_info tools, "
        "provide brief summary. %s");
#endif

    ai_skill_register("greet",
        "Greet the user",
        "You are rt-claw on ESP32-C3. Greet the user and describe "
        "your capabilities. %s");
}

int ai_skill_init(void)
{
    memset(s_skills, 0, sizeof(s_skills));
    s_count = 0;

    register_builtins();
    CLAW_LOGI(TAG, "initialized, %d built-in skills", s_count);
    return CLAW_OK;
}

int ai_skill_register(const char *name, const char *desc,
                      const char *prompt_template)
{
    if (!name || !prompt_template || s_count >= SKILL_MAX) {
        return CLAW_ERROR;
    }

    /* Check duplicate */
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_skills[i].name, name) == 0) {
            return CLAW_ERROR;
        }
    }

    ai_skill_t *s = &s_skills[s_count];
    snprintf(s->name, sizeof(s->name), "%s", name);
    s->description = desc;
    s->prompt_template = prompt_template;
    s_count++;

    return CLAW_OK;
}

int ai_skill_execute(const char *name, const char *params,
                     char *reply, size_t reply_size)
{
    if (!name || !reply || reply_size == 0) {
        return CLAW_ERROR;
    }

    const char *tmpl = ai_skill_find(name);
    if (!tmpl) {
        snprintf(reply, reply_size, "[skill '%s' not found]", name);
        return CLAW_ERROR;
    }

    char prompt[PROMPT_BUF_SIZE];
    snprintf(prompt, sizeof(prompt), tmpl,
             params ? params : "");

    return ai_chat_raw(prompt, reply, reply_size);
}

const char *ai_skill_find(const char *name)
{
    if (!name) {
        return NULL;
    }

    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_skills[i].name, name) == 0) {
            return s_skills[i].prompt_template;
        }
    }
    return NULL;
}

void ai_skill_list(void)
{
    CLAW_LOGI(TAG, "skills: %d/%d", s_count, SKILL_MAX);
    for (int i = 0; i < s_count; i++) {
        ai_skill_t *s = &s_skills[i];
        CLAW_LOGI(TAG, "  %-12s  %s", s->name,
                  s->description ? s->description : "");
    }
}
