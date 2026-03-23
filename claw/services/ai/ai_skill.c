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
#include "claw/core/console.h"
#include "claw/services/ai/ai_skill.h"
#include "claw/services/ai/ai_engine.h"
#include "claw/services/tools/tools.h"
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

/* --- KV persistence (platform-independent via OSAL) --- */

#include "osal/claw_kv.h"

#define SKILL_KV_NS    "claw_skill"
#define SKILL_KV_CNT   "cnt"
#define SKILL_KV_DATA  "data"

static int skill_persist(void)
{
    /* Count user-created skills */
    uint8_t cnt = 0;
    for (int i = 0; i < s_count; i++) {
        if (!s_skills[i].builtin) {
            cnt++;
        }
    }

    claw_kv_set_u8(SKILL_KV_NS, SKILL_KV_CNT, cnt);

    if (cnt > 0) {
        ai_skill_t buf[SKILL_MAX];
        int idx = 0;
        for (int i = 0; i < s_count; i++) {
            if (!s_skills[i].builtin) {
                memcpy(&buf[idx++], &s_skills[i], sizeof(ai_skill_t));
            }
        }
        return claw_kv_set_blob(SKILL_KV_NS, SKILL_KV_DATA, buf,
                                idx * sizeof(ai_skill_t));
    }

    claw_kv_delete(SKILL_KV_NS, SKILL_KV_DATA);
    return CLAW_OK;
}

static void skill_load(void)
{
    uint8_t cnt = 0;
    if (claw_kv_get_u8(SKILL_KV_NS, SKILL_KV_CNT, &cnt) != CLAW_OK
        || cnt == 0) {
        return;
    }

    ai_skill_t buf[SKILL_MAX];
    size_t blob_sz = cnt * sizeof(ai_skill_t);
    if (claw_kv_get_blob(SKILL_KV_NS, SKILL_KV_DATA,
                         buf, &blob_sz) != CLAW_OK) {
        return;
    }

    for (int i = 0; i < cnt && s_count < SKILL_MAX; i++) {
        memcpy(&s_skills[s_count], &buf[i], sizeof(ai_skill_t));
        s_skills[s_count].builtin = 0;
        s_count++;
    }

    CLAW_LOGI(TAG, "restored %d user skill(s) from KV", (int)cnt);
}

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
    skill_load();

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
            skill_persist();
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

int ai_skill_list_to_buf(char *buf, size_t size)
{
    if (!buf || size == 0) {
        return 0;
    }

    int off = snprintf(buf, size, "skills: %d/%d\n", s_count, SKILL_MAX);
    for (int i = 0; i < s_count && (size_t)off < size - 1; i++) {
        ai_skill_t *s = &s_skills[i];
        off += snprintf(buf + off, size - off,
                        "  %-12s  %s%s\n", s->name, s->description,
                        s->builtin ? " (built-in)" : "");
    }
    return off;
}

void ai_skill_list(void)
{
    char buf[512];
    ai_skill_list_to_buf(buf, sizeof(buf));
    claw_printf("%s", buf);
}

int ai_skill_count(void)
{
    return s_count;
}

const char *ai_skill_get_name(int index)
{
    if (index < 0 || index >= s_count) {
        return NULL;
    }
    return s_skills[index].name;
}

int ai_skill_try_command(const char *cmd_name, int argc, char **argv,
                         char *reply, size_t reply_size)
{
    if (!cmd_name || !reply || reply_size == 0) {
        return CLAW_ERROR;
    }

    /* Strip leading '/' */
    const char *name = cmd_name;
    if (name[0] == '/') {
        name++;
    }

    /* Check if skill exists */
    if (!ai_skill_find(name)) {
        return CLAW_ERROR;
    }

    /* Join argv[1..argc-1] as params */
    char params[256] = "";
    int off = 0;
    for (int i = 1; i < argc && off < (int)sizeof(params) - 1; i++) {
        if (i > 1) {
            params[off++] = ' ';
        }
        off += snprintf(params + off, sizeof(params) - off,
                        "%s", argv[i]);
    }

    return ai_skill_execute(name, params, reply, reply_size);
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

#if defined(CLAW_PLATFORM_ESP_IDF) && defined(CONFIG_RTCLAW_SKILL_ENABLE)

static claw_err_t tool_create_skill(struct claw_tool *tool,
                                    const cJSON *params, cJSON *result)
{
    (void)tool;
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

    skill_persist();

    cJSON_AddStringToObject(result, "status", "ok");
    char msg[64];
    snprintf(msg, sizeof(msg), "skill '%s' created",
             name_j->valuestring);
    cJSON_AddStringToObject(result, "message", msg);
    return CLAW_OK;
}

static claw_err_t tool_delete_skill(struct claw_tool *tool,
                                    const cJSON *params, cJSON *result)
{
    (void)tool;
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

#include "claw/core/tool.h"

static const struct claw_tool_ops create_skill_ops = {
    .execute = tool_create_skill,
};
static struct claw_tool create_skill_tool = {
    .name = "create_skill",
    .description =
        "Create a reusable prompt skill that persists across "
        "reboots. The skill can be invoked later via /skill <name>.",
    .input_schema_json = schema_create_skill,
    .ops = &create_skill_ops,
    .flags = CLAW_TOOL_LOCAL_ONLY,
};
CLAW_TOOL_REGISTER(create_skill, &create_skill_tool);

static const struct claw_tool_ops delete_skill_ops = {
    .execute = tool_delete_skill,
};
static struct claw_tool delete_skill_tool = {
    .name = "delete_skill",
    .description =
        "Delete a user-created skill. Built-in skills cannot "
        "be deleted.",
    .input_schema_json = schema_delete_skill,
    .ops = &delete_skill_ops,
    .flags = CLAW_TOOL_LOCAL_ONLY,
};
CLAW_TOOL_REGISTER(delete_skill, &delete_skill_tool);

#endif /* CLAW_PLATFORM_ESP_IDF && CONFIG_RTCLAW_SKILL_ENABLE */

/* OOP service registration */
#include "claw/core/service.h"
#ifdef CONFIG_RTCLAW_SKILL_ENABLE
static const char *ai_skill_deps[] = { "ai_engine", NULL };
CLAW_DEFINE_SIMPLE_SERVICE(ai_skill, "ai_skill",
    ai_skill_init, NULL, NULL, ai_skill_deps);
#endif
