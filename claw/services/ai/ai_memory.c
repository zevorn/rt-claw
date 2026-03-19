/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * AI memory — short-term (RAM ring buffer) + long-term (NVS Flash).
 */

#include "osal/claw_os.h"
#include "claw_config.h"
#include "claw/services/ai/ai_engine.h"
#include "claw/services/ai/ai_memory.h"

#include <string.h>
#include <stdio.h>

#define TAG "ai_mem"

/*
 * Short-term memory (RAM) — conversation turn ring buffer.
 * Uses cJSON for message serialization (available on all platforms).
 */

#ifdef CLAW_PLATFORM_ESP_IDF
#include "sdkconfig.h"
#endif

#ifdef CONFIG_RTCLAW_AI_MEMORY_MAX_MSGS
#define MEM_MAX_MSGS    CONFIG_RTCLAW_AI_MEMORY_MAX_MSGS
#else
#define MEM_MAX_MSGS    20
#endif

typedef struct {
    char    role[12];         /* "user" / "assistant" */
    char   *content_json;     /* heap: plain string or cJSON array string */
    uint8_t channel;          /* AI_CHANNEL_* */
} mem_entry_t;

static mem_entry_t s_entries[MEM_MAX_MSGS];
static int         s_count;
static struct claw_mutex *s_lock;

/*
 * Drop the oldest user+assistant pair for a given channel.
 * Falls back to dropping any oldest pair if no channel match.
 */
static void drop_oldest_pair_for(int channel)
{
    if (s_count < 2) {
        return;
    }

    /* Try to find a pair matching the target channel */
    for (int i = 0; i < s_count - 1; i++) {
        if (s_entries[i].channel == channel &&
            s_entries[i + 1].channel == channel) {
            claw_free(s_entries[i].content_json);
            claw_free(s_entries[i + 1].content_json);
            if (i + 2 < s_count) {
                memmove(&s_entries[i], &s_entries[i + 2],
                        (s_count - i - 2) * sizeof(mem_entry_t));
            }
            s_count -= 2;
            return;
        }
    }

    /* Fallback: drop the global oldest pair */
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

void ai_memory_add(const char *role, const char *content_json,
                   int channel)
{
    if (!role || !content_json) {
        return;
    }

    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);

    while (s_count >= MEM_MAX_MSGS) {
        drop_oldest_pair_for(channel);
    }

    mem_entry_t *e = &s_entries[s_count];
    snprintf(e->role, sizeof(e->role), "%s", role);
    e->channel = (uint8_t)channel;

    size_t len = strlen(content_json);
    e->content_json = claw_malloc(len + 1);
    if (e->content_json) {
        memcpy(e->content_json, content_json, len + 1);
        s_count++;
    }

    claw_mutex_unlock(s_lock);
}

/* Backward-compatible wrapper */
void ai_memory_add_message(const char *role, const char *content_json)
{
    ai_memory_add(role, content_json, AI_CHANNEL_SHELL);
}

cJSON *ai_memory_build(int channel)
{
    cJSON *messages = cJSON_CreateArray();
    if (!messages) {
        return NULL;
    }

    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);

    for (int i = 0; i < s_count; i++) {
        mem_entry_t *e = &s_entries[i];
        if (e->channel != channel) {
            continue;
        }

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

cJSON *ai_memory_build_messages(void)
{
    return ai_memory_build(AI_CHANNEL_SHELL);
}

void ai_memory_clear_channel(int channel)
{
    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);

    int dst = 0;
    for (int i = 0; i < s_count; i++) {
        if (s_entries[i].channel == channel) {
            claw_free(s_entries[i].content_json);
            s_entries[i].content_json = NULL;
        } else {
            if (dst != i) {
                s_entries[dst] = s_entries[i];
            }
            dst++;
        }
    }
    s_count = dst;

    claw_mutex_unlock(s_lock);
    CLAW_LOGI(TAG, "channel %d memory cleared", channel);
}

int ai_memory_count_channel(int channel)
{
    int n = 0;
    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);
    for (int i = 0; i < s_count; i++) {
        if (s_entries[i].channel == channel) {
            n++;
        }
    }
    claw_mutex_unlock(s_lock);
    return n;
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
 * Long-term memory (LTM).
 * ESP-IDF: persistent via NVS Flash.
 * Other platforms: RAM-only (lost on reboot).
 */

typedef struct {
    char key[LTM_KEY_MAX];
    char value[LTM_VALUE_MAX];
} ltm_entry_t;

static ltm_entry_t s_ltm[LTM_MAX_ENTRIES];
static int         s_ltm_count;

#include "osal/claw_kv.h"

#define LTM_KV_NS    "claw_ltm"
#define LTM_KV_DATA  "data"
#define LTM_KV_CNT   "cnt"

static int ltm_persist(void)
{
    if (claw_kv_set_u8(LTM_KV_NS, LTM_KV_CNT,
                       (uint8_t)s_ltm_count) != CLAW_OK) {
        return CLAW_ERROR;
    }
    return claw_kv_set_blob(LTM_KV_NS, LTM_KV_DATA, s_ltm,
                            s_ltm_count * sizeof(ltm_entry_t));
}

static void ltm_load(void)
{
    uint8_t cnt = 0;
    if (claw_kv_get_u8(LTM_KV_NS, LTM_KV_CNT, &cnt) != CLAW_OK) {
        s_ltm_count = 0;
        return;
    }
    if (cnt > LTM_MAX_ENTRIES) {
        cnt = LTM_MAX_ENTRIES;
    }
    size_t blob_size = cnt * sizeof(ltm_entry_t);
    if (claw_kv_get_blob(LTM_KV_NS, LTM_KV_DATA,
                         s_ltm, &blob_size) == CLAW_OK) {
        s_ltm_count = cnt;
    } else {
        s_ltm_count = 0;
    }
}

int ai_ltm_init(void)
{
    memset(s_ltm, 0, sizeof(s_ltm));
    s_ltm_count = 0;

    ltm_load();
    CLAW_LOGI(TAG, "long-term initialized, %d/%d entries",
              s_ltm_count, LTM_MAX_ENTRIES);
    return CLAW_OK;
}

int ai_ltm_save(const char *key, const char *value)
{
    if (!key || !value || key[0] == '\0') {
        return CLAW_ERROR;
    }

    for (int i = 0; i < s_ltm_count; i++) {
        if (strcmp(s_ltm[i].key, key) == 0) {
            snprintf(s_ltm[i].value, LTM_VALUE_MAX, "%s", value);
            CLAW_LOGI(TAG, "ltm updated: %s", key);
            return ltm_persist();
        }
    }

    if (s_ltm_count >= LTM_MAX_ENTRIES) {
        CLAW_LOGE(TAG, "ltm full (%d entries)", LTM_MAX_ENTRIES);
        return CLAW_ERROR;
    }

    ltm_entry_t *e = &s_ltm[s_ltm_count];
    snprintf(e->key, LTM_KEY_MAX, "%s", key);
    snprintf(e->value, LTM_VALUE_MAX, "%s", value);
    s_ltm_count++;

    CLAW_LOGI(TAG, "ltm saved: %s", key);
    return ltm_persist();
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
            if (i < s_ltm_count - 1) {
                memmove(&s_ltm[i], &s_ltm[i + 1],
                        (s_ltm_count - i - 1) * sizeof(ltm_entry_t));
            }
            s_ltm_count--;
            memset(&s_ltm[s_ltm_count], 0, sizeof(ltm_entry_t));

            CLAW_LOGI(TAG, "ltm deleted: %s", key);
            return ltm_persist();
        }
    }

    return CLAW_ERROR;
}

void ai_ltm_list(void)
{
    printf("long-term memory: %d/%d entries\n",
           s_ltm_count, LTM_MAX_ENTRIES);
    for (int i = 0; i < s_ltm_count; i++) {
        printf("  [%d] %-24s = %.60s\n", i,
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
