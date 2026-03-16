/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Heartbeat — periodic AI check-in.
 *
 * Collects events posted by other services.  On each scheduler
 * tick the callback checks the event buffer:
 *   - Empty  → skip (no LLM call, zero token cost)
 *   - Events → spawn a thread to call ai_chat_raw(), push
 *              the summary to IM or serial console.
 */

#include "osal/claw_os.h"
#include "claw_config.h"
#include "claw/core/heartbeat.h"
#include "claw/core/scheduler.h"
#include "claw/services/ai/ai_engine.h"

#include <string.h>
#include <stdio.h>

#ifdef CLAW_PLATFORM_ESP_IDF
#include "esp_heap_caps.h"
#elif defined(CLAW_PLATFORM_RTTHREAD)
#include <rtthread.h>
#endif

#define TAG "heartbeat"

/* LLM connectivity state: -1 = unknown, 0 = offline, 1 = online */
static int s_llm_state = -1;

/* Single event entry */
typedef struct {
    char     category[16];
    char     message[CLAW_HEARTBEAT_MSG_MAX];
    uint32_t timestamp;
} hb_event_t;

static hb_event_t  s_events[CLAW_HEARTBEAT_MAX_EVENTS];
static int          s_event_count;
static claw_mutex_t s_lock;
static int          s_busy;

/* IM reply destination (optional) */
static heartbeat_reply_fn_t s_reply_fn;
static char s_reply_target[64];
static claw_mutex_t s_reply_lock;

/*
 * Build a prompt from buffered events and clear the buffer.
 * Caller must hold s_lock.
 */
static int build_prompt(char *buf, size_t size)
{
    int off = snprintf(buf, size,
        "You are a system monitor on an embedded device. "
        "Below are events collected since the last check-in. "
        "Summarize anything actionable for the user in 1-3 "
        "short sentences. If nothing needs attention, respond "
        "with exactly: HEARTBEAT_OK\n\nEvents:\n");

    for (int i = 0; i < s_event_count && (size_t)off < size - 1; i++) {
        off += snprintf(buf + off, size - off, "[%s] %s\n",
                        s_events[i].category,
                        s_events[i].message);
    }

    int count = s_event_count;
    s_event_count = 0;
    return count;
}

/* Deliver a heartbeat summary to IM or console */
static void deliver(const char *text)
{
    claw_mutex_lock(s_reply_lock, CLAW_WAIT_FOREVER);
    heartbeat_reply_fn_t fn = s_reply_fn;
    char target[64];
    snprintf(target, sizeof(target), "%s", s_reply_target);
    claw_mutex_unlock(s_reply_lock);

    if (fn) {
        fn(target, text);
    } else {
        printf("\n\033[0;36m<heartbeat>\033[0m %s\n", text);
        fflush(stdout);
    }
}

/* Thread that runs the AI call — created per heartbeat tick */
static void heartbeat_ai_thread(void *arg)
{
    (void)arg;
    char *prompt = claw_malloc(CLAW_HEARTBEAT_PROMPT_MAX);
    char *reply  = claw_malloc(CLAW_HEARTBEAT_REPLY_MAX);

    if (!prompt || !reply) {
        CLAW_LOGE(TAG, "alloc failed");
        goto out;
    }

    /* Snapshot events under lock */
    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);
    int n = build_prompt(prompt, CLAW_HEARTBEAT_PROMPT_MAX);
    claw_mutex_unlock(s_lock);

    if (n == 0) {
        goto out;
    }

    CLAW_LOGD(TAG, "checking %d event(s)", n);

    if (ai_chat_raw(prompt, reply, CLAW_HEARTBEAT_REPLY_MAX)
            != CLAW_OK) {
        CLAW_LOGW(TAG, "AI call failed");
        goto out;
    }

    /* Skip delivery if the model says nothing needs attention */
    if (strstr(reply, "HEARTBEAT_OK") != NULL) {
        CLAW_LOGD(TAG, "nothing actionable");
        goto out;
    }

    deliver(reply);

out:
    claw_free(prompt);
    claw_free(reply);

    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);
    s_busy = 0;
    claw_mutex_unlock(s_lock);
}

/*
 * Device health check — collect anomalies and post events.
 * Runs before the LLM ping so anomalies are included in the
 * next heartbeat AI summary.
 */
static void check_device_health(void)
{
    uint32_t free_kb = 0;
    uint32_t total_kb = 0;

#ifdef CLAW_PLATFORM_ESP_IDF
    size_t free_sz = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    size_t total_sz = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    free_kb = (uint32_t)(free_sz / 1024);
    total_kb = (uint32_t)(total_sz / 1024);
#elif defined(CLAW_PLATFORM_RTTHREAD)
    rt_size_t total_rt = 0, used_rt = 0, max_rt = 0;
    rt_memory_info(&total_rt, &used_rt, &max_rt);
    free_kb = (uint32_t)((total_rt - used_rt) / 1024);
    total_kb = (uint32_t)(total_rt / 1024);
#endif

    if (total_kb == 0) {
        return;
    }

    uint32_t used_pct = 100 - (free_kb * 100 / total_kb);

    /* Alert when heap usage exceeds 80% */
    if (used_pct > 80) {
        char msg[80];
        snprintf(msg, sizeof(msg),
                 "heap %lu%% used (%luKB free / %luKB total)",
                 (unsigned long)used_pct,
                 (unsigned long)free_kb,
                 (unsigned long)total_kb);
        heartbeat_post("memory", msg);
    }
}

/* LLM connectivity probe — runs in its own thread */
static void ping_thread(void *arg)
{
    (void)arg;

    check_device_health();

    int ok = (ai_ping() == CLAW_OK) ? 1 : 0;
    int prev = s_llm_state;
    s_llm_state = ok;

    if (prev != ok) {
        const char *msg = ok
            ? "LLM API is back online"
            : "LLM API is unreachable";
        CLAW_LOGI(TAG, "%s", msg);
        deliver(msg);
    } else {
        CLAW_LOGD(TAG, "ping: %s", ok ? "online" : "offline");
    }

    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);
    s_busy = 0;
    claw_mutex_unlock(s_lock);
}

/* Scheduler callback — runs in sched thread, must not block */
static void heartbeat_tick(void *arg)
{
    (void)arg;

    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);

    if (s_busy) {
        claw_mutex_unlock(s_lock);
        return;
    }

    if (s_event_count == 0) {
        /* No events — do a lightweight LLM ping instead */
        s_busy = 1;
        claw_mutex_unlock(s_lock);

        claw_thread_t th = claw_thread_create(
            "hb_ping", ping_thread, NULL, 4096, 20);
        if (!th) {
            CLAW_LOGE(TAG, "ping thread create failed");
            claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);
            s_busy = 0;
            claw_mutex_unlock(s_lock);
        }
        return;
    }

    s_busy = 1;
    claw_mutex_unlock(s_lock);

    claw_thread_t th = claw_thread_create(
        "hb_ai", heartbeat_ai_thread, NULL,
        CLAW_HEARTBEAT_THREAD_STACK, 20);
    if (!th) {
        CLAW_LOGE(TAG, "thread create failed");
        claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);
        s_busy = 0;
        claw_mutex_unlock(s_lock);
    }
}

/* --- Public API --- */

int heartbeat_init(void)
{
    s_lock = claw_mutex_create("hb");
    s_reply_lock = claw_mutex_create("hb_rp");
    if (!s_lock || !s_reply_lock) {
        CLAW_LOGE(TAG, "mutex create failed");
        if (s_lock) {
            claw_mutex_delete(s_lock);
        }
        if (s_reply_lock) {
            claw_mutex_delete(s_reply_lock);
        }
        return CLAW_ERROR;
    }

    memset(s_events, 0, sizeof(s_events));
    s_event_count = 0;
    s_busy = 0;
    s_reply_fn = NULL;
    s_reply_target[0] = '\0';

    int ret = sched_add("heartbeat",
                        CLAW_HEARTBEAT_INTERVAL_MS,
                        -1, heartbeat_tick, NULL);
    if (ret != CLAW_OK) {
        CLAW_LOGE(TAG, "failed to register with scheduler");
        return ret;
    }

    CLAW_LOGI(TAG, "initialized, interval=%ds, max_events=%d",
              CLAW_HEARTBEAT_INTERVAL_MS / 1000,
              CLAW_HEARTBEAT_MAX_EVENTS);
    return CLAW_OK;
}

void heartbeat_post(const char *category, const char *message)
{
    if (!category || !message) {
        return;
    }

    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);

    if (s_event_count < CLAW_HEARTBEAT_MAX_EVENTS) {
        hb_event_t *e = &s_events[s_event_count];
        snprintf(e->category, sizeof(e->category), "%s", category);
        snprintf(e->message, sizeof(e->message), "%s", message);
        e->timestamp = claw_tick_ms();
        s_event_count++;
    } else {
        CLAW_LOGW(TAG, "event buffer full, dropping: [%s] %s",
                  category, message);
    }

    claw_mutex_unlock(s_lock);
}

void heartbeat_set_reply(heartbeat_reply_fn_t fn,
                         const char *target)
{
    claw_mutex_lock(s_reply_lock, CLAW_WAIT_FOREVER);
    s_reply_fn = fn;
    if (target) {
        snprintf(s_reply_target, sizeof(s_reply_target),
                 "%s", target);
    } else {
        s_reply_target[0] = '\0';
    }
    claw_mutex_unlock(s_reply_lock);
}

int heartbeat_llm_online(void)
{
    return s_llm_state;
}
