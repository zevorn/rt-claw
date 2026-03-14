/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * AI skill — predefined + user-created prompt templates.
 *
 * Built-in skills are compiled in.  User skills are created at
 * runtime via LLM Tool Use (create_skill) and persisted to NVS.
 */

#include "osal/claw_os.h"
#include "claw/services/ai/ai_skill.h"
#include "claw/services/ai/ai_engine.h"
#include "claw/tools/claw_tools.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>

#define TAG "ai_skill"
#define PROMPT_BUF_SIZE 512

typedef struct {
    char name[SKILL_NAME_MAX];
    char description[SKILL_DESC_MAX];
    char prompt_template[SKILL_PROMPT_MAX];
    int  builtin;   /* 1 = compiled-in, 0 = user-created */
} ai_skill_t;

static ai_skill_t s_skills[SKILL_MAX];
static int        s_count;

/* --- NVS persistence (ESP-IDF only) --- */

#ifdef CLAW_PLATFORM_ESP_IDF
#include "nvs_flash.h"
#include "nvs.h"

#define SKILL_NVS_NS    "claw_skill"
#define SKILL_NVS_CNT   "cnt"
#define SKILL_NVS_DATA  "data"

static int skill_flush_nvs(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(SKILL_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        CLAW_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
        return CLAW_ERROR;
    }

    /* Count user-created skills */
    uint8_t cnt = 0;
    for (int i = 0; i < s_count; i++) {
        if (!s_skills[i].builtin) {
            cnt++;
        }
    }

    nvs_set_u8(h, SKILL_NVS_CNT, cnt);

    if (cnt > 0) {
        /*
         * Pack user skills into a contiguous blob.
         * Each entry: ai_skill_t (fixed size).
         */
        ai_skill_t buf[SKILL_MAX];
        int idx = 0;
        for (int i = 0; i < s_count; i++) {
            if (!s_skills[i].builtin) {
                memcpy(&buf[idx++], &s_skills[i], sizeof(ai_skill_t));
            }
        }
        nvs_set_blob(h, SKILL_NVS_DATA, buf,
                     idx * sizeof(ai_skill_t));
    } else {
        nvs_erase_key(h, SKILL_NVS_DATA);
    }

    err = nvs_commit(h);
    nvs_close(h);
    return (err == ESP_OK) ? CLAW_OK : CLAW_ERROR;
}

static void skill_load_nvs(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(SKILL_NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return;
    }

    uint8_t cnt = 0;
    err = nvs_get_u8(h, SKILL_NVS_CNT, &cnt);
    if (err != ESP_OK || cnt == 0) {
        nvs_close(h);
        return;
    }

    ai_skill_t buf[SKILL_MAX];
    size_t blob_sz = cnt * sizeof(ai_skill_t);
    err = nvs_get_blob(h, SKILL_NVS_DATA, buf, &blob_sz);
    nvs_close(h);

    if (err != ESP_OK) {
        return;
    }

    for (int i = 0; i < cnt && s_count < SKILL_MAX; i++) {
        memcpy(&s_skills[s_count], &buf[i], sizeof(ai_skill_t));
        s_skills[s_count].builtin = 0;
        s_count++;
    }

    CLAW_LOGI(TAG, "restored %d user skill(s) from NVS", (int)cnt);
}

#else /* non-ESP-IDF */

static int  skill_flush_nvs(void) { return CLAW_OK; }
static void skill_load_nvs(void)  {}

#endif

/* --- Built-in skill registration --- */

static void register_builtins(void)
{
#ifdef CONFIG_RTCLAW_TOOL_LCD
    if (claw_lcd_available()) {
        ai_skill_register("draw",
            "Draw on LCD display",
            "Draw the following on the LCD display "
            "using lcd_* tools: %s");
    }
#endif

#ifdef CONFIG_RTCLAW_TOOL_SYSTEM
    ai_skill_register("monitor",
        "Check system health",
        "Check system health via system_info and memory_info "
        "tools, provide brief summary. %s");
#endif

    ai_skill_register("greet",
        "Greet the user",
        "You are rt-claw on an embedded RTOS device. "
        "Greet the user and describe your capabilities. %s");

    /* Mark all current entries as built-in */
    for (int i = 0; i < s_count; i++) {
        s_skills[i].builtin = 1;
    }
}

/* --- Public API --- */

int ai_skill_init(void)
{
    memset(s_skills, 0, sizeof(s_skills));
    s_count = 0;

    register_builtins();
    skill_load_nvs();

    CLAW_LOGI(TAG, "initialized, %d skill(s)", s_count);
    return CLAW_OK;
}

int ai_skill_register(const char *name, const char *desc,
                      const char *prompt_template)
{
    if (!name || !prompt_template || s_count >= SKILL_MAX) {
        return CLAW_ERROR;
    }

    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_skills[i].name, name) == 0) {
            return CLAW_ERROR;
        }
    }

    ai_skill_t *s = &s_skills[s_count];
    snprintf(s->name, sizeof(s->name), "%s", name);
    snprintf(s->description, sizeof(s->description), "%s",
             desc ? desc : "");
    snprintf(s->prompt_template, sizeof(s->prompt_template),
             "%s", prompt_template);
    s->builtin = 0;
    s_count++;

    return CLAW_OK;
}

int ai_skill_remove(const char *name)
{
    if (!name) {
        return CLAW_ERROR;
    }

    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_skills[i].name, name) == 0) {
            if (s_skills[i].builtin) {
                return CLAW_ERROR;  /* cannot remove built-in */
            }
            if (i < s_count - 1) {
                memmove(&s_skills[i], &s_skills[i + 1],
                        (s_count - i - 1) * sizeof(ai_skill_t));
            }
            s_count--;
            memset(&s_skills[s_count], 0, sizeof(ai_skill_t));
            skill_flush_nvs();
            CLAW_LOGI(TAG, "removed skill: %s", name);
            return CLAW_OK;
        }
    }
    return CLAW_ERROR;
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
    printf("skills: %d/%d\n", s_count, SKILL_MAX);
    for (int i = 0; i < s_count; i++) {
        ai_skill_t *s = &s_skills[i];
        printf("  %-12s  %s%s\n", s->name, s->description,
               s->builtin ? " (built-in)" : "");
    }
}

int ai_skill_count(void)
{
    return s_count;
}

char *ai_skill_build_summary(void)
{
    if (s_count == 0) {
        return NULL;
    }

    size_t buf_sz = 64 + s_count * (SKILL_NAME_MAX + SKILL_DESC_MAX + 16);
    char *buf = claw_malloc(buf_sz);
    if (!buf) {
        return NULL;
    }

    int off = snprintf(buf, buf_sz,
                       "\n\nAvailable skills (/skill <name> [args]):\n");
    for (int i = 0; i < s_count && (size_t)off < buf_sz - 1; i++) {
        off += snprintf(buf + off, buf_sz - off,
                        "- %s: %s\n",
                        s_skills[i].name, s_skills[i].description);
    }
    return buf;
}

/* --- LLM Tool Use: create_skill / delete_skill --- */

#ifdef CLAW_PLATFORM_ESP_IDF

static int tool_create_skill(const cJSON *params, cJSON *result)
{
    cJSON *name_j = cJSON_GetObjectItem(params, "name");
    cJSON *desc_j = cJSON_GetObjectItem(params, "description");
    cJSON *tmpl_j = cJSON_GetObjectItem(params, "prompt_template");

    if (!name_j || !cJSON_IsString(name_j) ||
        !tmpl_j || !cJSON_IsString(tmpl_j)) {
        cJSON_AddStringToObject(result, "error",
                                "missing name or prompt_template");
        return CLAW_ERROR;
    }

    const char *desc = (desc_j && cJSON_IsString(desc_j))
                       ? desc_j->valuestring : "";

    if (ai_skill_register(name_j->valuestring, desc,
                          tmpl_j->valuestring) != CLAW_OK) {
        cJSON_AddStringToObject(result, "error",
                                "skill registry full or duplicate");
        return CLAW_ERROR;
    }

    skill_flush_nvs();

    cJSON_AddStringToObject(result, "status", "ok");
    char msg[64];
    snprintf(msg, sizeof(msg), "skill '%s' created",
             name_j->valuestring);
    cJSON_AddStringToObject(result, "message", msg);
    return CLAW_OK;
}

static int tool_delete_skill(const cJSON *params, cJSON *result)
{
    cJSON *name_j = cJSON_GetObjectItem(params, "name");

    if (!name_j || !cJSON_IsString(name_j)) {
        cJSON_AddStringToObject(result, "error", "missing name");
        return CLAW_ERROR;
    }

    if (ai_skill_remove(name_j->valuestring) != CLAW_OK) {
        cJSON_AddStringToObject(result, "error",
                                "skill not found or is built-in");
        return CLAW_ERROR;
    }

    cJSON_AddStringToObject(result, "status", "ok");
    char msg[64];
    snprintf(msg, sizeof(msg), "skill '%s' deleted",
             name_j->valuestring);
    cJSON_AddStringToObject(result, "message", msg);
    return CLAW_OK;
}

static const char schema_create_skill[] =
    "{\"type\":\"object\","
    "\"properties\":{"
    "\"name\":{\"type\":\"string\","
    "\"description\":\"Unique skill name (one word)\"},"
    "\"description\":{\"type\":\"string\","
    "\"description\":\"Short description of the skill\"},"
    "\"prompt_template\":{\"type\":\"string\","
    "\"description\":\"Prompt template. Use %s where user "
    "parameters should be inserted.\"}},"
    "\"required\":[\"name\",\"prompt_template\"]}";

static const char schema_delete_skill[] =
    "{\"type\":\"object\","
    "\"properties\":{"
    "\"name\":{\"type\":\"string\","
    "\"description\":\"Name of skill to delete\"}},"
    "\"required\":[\"name\"]}";

void claw_tools_register_skill(void)
{
    claw_tool_register("create_skill",
        "Create a reusable prompt skill that persists across "
        "reboots. The skill can be invoked later via /skill <name>.",
        schema_create_skill, tool_create_skill);

    claw_tool_register("delete_skill",
        "Delete a user-created skill. Built-in skills cannot "
        "be deleted.",
        schema_delete_skill, tool_delete_skill);
}

#else /* non-ESP-IDF */

void claw_tools_register_skill(void) {}

#endif
