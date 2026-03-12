/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 *
 * AI memory — short-term (RAM ring buffer) + long-term (NVS Flash).
 */

#include "claw_os.h"
#include "ai_memory.h"

#include <string.h>
#include <stdio.h>

#define TAG "ai_mem"

/*
 * Short-term memory (RAM) — conversation turn ring buffer
 */

#ifdef CLAW_PLATFORM_ESP_IDF

#include "sdkconfig.h"
#include "cJSON.h"

#ifdef CONFIG_CLAW_AI_MEMORY_MAX_MSGS
#define MEM_MAX_MSGS    CONFIG_CLAW_AI_MEMORY_MAX_MSGS
#else
#define MEM_MAX_MSGS    20
#endif

typedef struct {
    char  role[12];         /* "user" / "assistant" */
    char *content_json;     /* heap: plain string or cJSON array string */
} mem_entry_t;

static mem_entry_t s_entries[MEM_MAX_MSGS];
static int         s_count;
static claw_mutex_t s_lock;

static void drop_oldest_pair(void)
{
    if (s_count < 2) {
        return;
    }

    claw_free(s_entries[0].content_json);
    claw_free(s_entries[1].content_json);

    memmove(&s_entries[0], &s_entries[2],
            (s_count - 2) * sizeof(mem_entry_t));
    s_count -= 2;
}

int ai_memory_init(void)
{
    s_lock = claw_mutex_create("ai_mem");
    if (!s_lock) {
        CLAW_LOGE(TAG, "mutex create failed");
        return CLAW_ERROR;
    }
    memset(s_entries, 0, sizeof(s_entries));
    s_count = 0;
    CLAW_LOGI(TAG, "short-term initialized, max_msgs=%d", MEM_MAX_MSGS);
    return CLAW_OK;
}

void ai_memory_add_message(const char *role, const char *content_json)
{
    if (!role || !content_json) {
        return;
    }

    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);

    /* Make room if full */
    while (s_count >= MEM_MAX_MSGS) {
        drop_oldest_pair();
    }

    mem_entry_t *e = &s_entries[s_count];
    snprintf(e->role, sizeof(e->role), "%s", role);

    size_t len = strlen(content_json);
    e->content_json = claw_malloc(len + 1);
    if (e->content_json) {
        memcpy(e->content_json, content_json, len + 1);
        s_count++;
    }

    claw_mutex_unlock(s_lock);
}

cJSON *ai_memory_build_messages(void)
{
    cJSON *messages = cJSON_CreateArray();
    if (!messages) {
        return NULL;
    }

    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);

    for (int i = 0; i < s_count; i++) {
        mem_entry_t *e = &s_entries[i];
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "role", e->role);

        /* Parse JSON array content (tool_use/tool_result) */
        if (e->content_json[0] == '[') {
            cJSON *arr = cJSON_Parse(e->content_json);
            if (arr) {
                cJSON_AddItemToObject(msg, "content", arr);
            } else {
                cJSON_AddStringToObject(msg, "content", e->content_json);
            }
        } else {
            cJSON_AddStringToObject(msg, "content", e->content_json);
        }

        cJSON_AddItemToArray(messages, msg);
    }

    claw_mutex_unlock(s_lock);
    return messages;
}

void ai_memory_clear(void)
{
    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);

    for (int i = 0; i < s_count; i++) {
        claw_free(s_entries[i].content_json);
        s_entries[i].content_json = NULL;
    }
    s_count = 0;

    claw_mutex_unlock(s_lock);
    CLAW_LOGI(TAG, "short-term memory cleared");
}

int ai_memory_count(void)
{
    return s_count;
}

/*
 * Long-term memory (NVS Flash) — persistent key-value facts
 */

#include "nvs_flash.h"
#include "nvs.h"

#define LTM_NVS_NAMESPACE  "claw_ltm"
#define LTM_NVS_KEY_DATA   "data"
#define LTM_NVS_KEY_COUNT  "cnt"

typedef struct {
    char key[LTM_KEY_MAX];
    char value[LTM_VALUE_MAX];
} ltm_entry_t;

static ltm_entry_t s_ltm[LTM_MAX_ENTRIES];
static int         s_ltm_count;

/* Flush entire LTM array to NVS */
static int ltm_flush_nvs(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(LTM_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        CLAW_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return CLAW_ERROR;
    }

    nvs_set_u8(h, LTM_NVS_KEY_COUNT, (uint8_t)s_ltm_count);
    nvs_set_blob(h, LTM_NVS_KEY_DATA, s_ltm,
                 s_ltm_count * sizeof(ltm_entry_t));
    err = nvs_commit(h);
    nvs_close(h);

    if (err != ESP_OK) {
        CLAW_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
        return CLAW_ERROR;
    }
    return CLAW_OK;
}

/* Load LTM array from NVS */
static int ltm_load_nvs(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(LTM_NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* First boot, no data yet */
        s_ltm_count = 0;
        return CLAW_OK;
    }
    if (err != ESP_OK) {
        CLAW_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return CLAW_ERROR;
    }

    uint8_t cnt = 0;
    err = nvs_get_u8(h, LTM_NVS_KEY_COUNT, &cnt);
    if (err != ESP_OK) {
        nvs_close(h);
        s_ltm_count = 0;
        return CLAW_OK;
    }

    if (cnt > LTM_MAX_ENTRIES) {
        cnt = LTM_MAX_ENTRIES;
    }

    size_t blob_size = cnt * sizeof(ltm_entry_t);
    err = nvs_get_blob(h, LTM_NVS_KEY_DATA, s_ltm, &blob_size);
    nvs_close(h);

    if (err == ESP_OK) {
        s_ltm_count = cnt;
    } else {
        s_ltm_count = 0;
    }

    return CLAW_OK;
}

int ai_ltm_init(void)
{
    memset(s_ltm, 0, sizeof(s_ltm));
    s_ltm_count = 0;

    ltm_load_nvs();
    CLAW_LOGI(TAG, "long-term initialized, %d/%d entries from flash",
              s_ltm_count, LTM_MAX_ENTRIES);
    return CLAW_OK;
}

int ai_ltm_save(const char *key, const char *value)
{
    if (!key || !value || key[0] == '\0') {
        return CLAW_ERROR;
    }

    /* Update existing entry if key matches */
    for (int i = 0; i < s_ltm_count; i++) {
        if (strcmp(s_ltm[i].key, key) == 0) {
            snprintf(s_ltm[i].value, LTM_VALUE_MAX, "%s", value);
            CLAW_LOGI(TAG, "ltm updated: %s", key);
            return ltm_flush_nvs();
        }
    }

    /* Add new entry */
    if (s_ltm_count >= LTM_MAX_ENTRIES) {
        CLAW_LOGE(TAG, "ltm full (%d entries)", LTM_MAX_ENTRIES);
        return CLAW_ERROR;
    }

    ltm_entry_t *e = &s_ltm[s_ltm_count];
    snprintf(e->key, LTM_KEY_MAX, "%s", key);
    snprintf(e->value, LTM_VALUE_MAX, "%s", value);
    s_ltm_count++;

    CLAW_LOGI(TAG, "ltm saved: %s", key);
    return ltm_flush_nvs();
}

int ai_ltm_load(const char *key, char *value, size_t size)
{
    if (!key || !value || size == 0) {
        return CLAW_ERROR;
    }

    for (int i = 0; i < s_ltm_count; i++) {
        if (strcmp(s_ltm[i].key, key) == 0) {
            snprintf(value, size, "%s", s_ltm[i].value);
            return CLAW_OK;
        }
    }

    return CLAW_ERROR;
}

int ai_ltm_delete(const char *key)
{
    if (!key) {
        return CLAW_ERROR;
    }

    for (int i = 0; i < s_ltm_count; i++) {
        if (strcmp(s_ltm[i].key, key) == 0) {
            /* Shift remaining entries down */
            if (i < s_ltm_count - 1) {
                memmove(&s_ltm[i], &s_ltm[i + 1],
                        (s_ltm_count - i - 1) * sizeof(ltm_entry_t));
            }
            s_ltm_count--;
            memset(&s_ltm[s_ltm_count], 0, sizeof(ltm_entry_t));

            CLAW_LOGI(TAG, "ltm deleted: %s", key);
            return ltm_flush_nvs();
        }
    }

    return CLAW_ERROR;
}

void ai_ltm_list(void)
{
    CLAW_LOGI(TAG, "long-term memory: %d/%d entries",
              s_ltm_count, LTM_MAX_ENTRIES);
    for (int i = 0; i < s_ltm_count; i++) {
        CLAW_LOGI(TAG, "  [%d] %-24s = %.60s", i,
                  s_ltm[i].key, s_ltm[i].value);
    }
}

int ai_ltm_count(void)
{
    return s_ltm_count;
}

char *ai_ltm_build_context(void)
{
    if (s_ltm_count == 0) {
        return NULL;
    }

    /* Estimate buffer: header + per-entry (key + value + formatting) */
    size_t buf_size = 64 + s_ltm_count * (LTM_KEY_MAX + LTM_VALUE_MAX + 8);
    char *buf = claw_malloc(buf_size);
    if (!buf) {
        return NULL;
    }

    int off = snprintf(buf, buf_size,
                       "\n\nLong-term memory (facts you have learned):\n");

    for (int i = 0; i < s_ltm_count; i++) {
        off += snprintf(buf + off, buf_size - off,
                        "- %s: %s\n", s_ltm[i].key, s_ltm[i].value);
    }

    return buf;
}

#else /* non-ESP-IDF platforms */

int ai_memory_init(void)
{
    CLAW_LOGI(TAG, "initialized (stub)");
    return CLAW_OK;
}

void ai_memory_add_message(const char *role, const char *content_json)
{
    (void)role;
    (void)content_json;
}

void ai_memory_clear(void) {}
int  ai_memory_count(void) { return 0; }

int ai_ltm_init(void)
{
    CLAW_LOGI(TAG, "long-term initialized (stub)");
    return CLAW_OK;
}

int ai_ltm_save(const char *key, const char *value)
{
    (void)key;
    (void)value;
    return CLAW_ERROR;
}

int ai_ltm_load(const char *key, char *value, size_t size)
{
    (void)key;
    (void)value;
    (void)size;
    return CLAW_ERROR;
}

int ai_ltm_delete(const char *key)
{
    (void)key;
    return CLAW_ERROR;
}

void ai_ltm_list(void) {}
int  ai_ltm_count(void) { return 0; }

char *ai_ltm_build_context(void)
{
    return NULL;
}

#endif
