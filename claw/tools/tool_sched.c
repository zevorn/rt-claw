/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Scheduler tools — let AI add/remove timed tasks via Tool Use.
 *
 * A dedicated worker thread executes AI calls asynchronously so
 * the scheduler callback returns immediately and never blocks
 * the scheduler or the interactive shell.
 */

#include "osal/claw_os.h"
#include "claw/tools/claw_tools.h"
#include "claw/core/scheduler.h"
#include "claw/services/ai/ai_engine.h"

#include <string.h>
#include <stdio.h>

#ifdef CLAW_PLATFORM_ESP_IDF

#include "cJSON.h"
#include "nvs_flash.h"
#include "nvs.h"

#define TAG              "tool_sched"
#define SCHED_AI_MAX     4
#define SCHED_PROMPT_MAX 256
#define SCHED_REPLY_MAX  1024
#define REPLY_TARGET_MAX 64
#define WORKER_STACK     16384
#define WORKER_PRIO      11

#define SCHED_NVS_NS     "claw_sched"

/* Persistent task record — stored in NVS blob */
typedef struct __attribute__((packed)) {
    char     name[24];
    char     prompt[SCHED_PROMPT_MAX];
    uint32_t interval_s;
    int32_t  count;
    char     reply_target[REPLY_TARGET_MAX];
} sched_persist_t;

typedef struct {
    char name[24];
    char prompt[SCHED_PROMPT_MAX];
    char reply[SCHED_REPLY_MAX];
    int  in_use;
    uint32_t interval_s;        /* for NVS persistence */
    int32_t  count;             /* for NVS persistence */
    sched_reply_fn_t reply_fn;
    char reply_target[REPLY_TARGET_MAX];
} sched_ai_ctx_t;

static sched_ai_ctx_t s_ctx[SCHED_AI_MAX];

/* Worker thread state */
static claw_sem_t   s_worker_sem;
static claw_mutex_t s_worker_lock;
static sched_ai_ctx_t *s_pending_ctx;
static int s_worker_busy; /* protected by s_worker_lock */

/*
 * Reply context — set by the caller (feishu / shell) before ai_chat()
 * so that tool_schedule_task() can capture the destination.
 * Protected by s_rctx_lock; read inside ai_chat() tool execution
 * while s_api_lock is held, so races are practically impossible
 * but we lock anyway for correctness.
 */
static claw_mutex_t      s_rctx_lock;
static sched_reply_fn_t  s_rctx_fn;
static char              s_rctx_target[REPLY_TARGET_MAX];

void sched_set_reply_context(sched_reply_fn_t fn, const char *target)
{
    if (!s_rctx_lock) {
        return;
    }
    claw_mutex_lock(s_rctx_lock, CLAW_WAIT_FOREVER);
    s_rctx_fn = fn;
    if (target) {
        snprintf(s_rctx_target, sizeof(s_rctx_target), "%s", target);
    } else {
        s_rctx_target[0] = '\0';
    }
    claw_mutex_unlock(s_rctx_lock);
}

static sched_ai_ctx_t *ctx_alloc(void)
{
    for (int i = 0; i < SCHED_AI_MAX; i++) {
        if (!s_ctx[i].in_use) {
            s_ctx[i].in_use = 1;
            return &s_ctx[i];
        }
    }
    return NULL;
}

static void ctx_free_by_name(const char *name)
{
    for (int i = 0; i < SCHED_AI_MAX; i++) {
        if (s_ctx[i].in_use &&
            strcmp(s_ctx[i].name, name) == 0) {
            s_ctx[i].in_use = 0;
            return;
        }
    }
}

/* Worker thread — runs AI calls with sufficient stack */
static void ai_worker_thread(void *arg)
{
    (void)arg;

    while (1) {
        claw_sem_take(s_worker_sem, CLAW_WAIT_FOREVER);

        claw_mutex_lock(s_worker_lock, CLAW_WAIT_FOREVER);
        sched_ai_ctx_t *ctx = s_pending_ctx;
        s_pending_ctx = NULL;
        s_worker_busy = 1;
        claw_mutex_unlock(s_worker_lock);

        if (!ctx) {
            s_worker_busy = 0;
            continue;
        }

        /*
         * Late-bind reply function for NVS-restored tasks.
         * On boot, reply_fn is NULL because function pointers
         * cannot be serialized.  If reply_target is set (from
         * NVS) and the IM channel has registered a callback
         * via sched_set_reply_context(), use it.
         */
        if (!ctx->reply_fn && ctx->reply_target[0] != '\0') {
            claw_mutex_lock(s_rctx_lock, CLAW_WAIT_FOREVER);
            ctx->reply_fn = s_rctx_fn;
            claw_mutex_unlock(s_rctx_lock);
        }

        if (ctx->reply_fn) {
            ai_set_channel_hint(
                " This is a scheduled background task."
                " Deliver results as concise text to the"
                " messaging channel. Do NOT use LCD display"
                " or mention serial console.");
        } else {
            ai_set_channel_hint(
                " This is a scheduled background task."
                " Output to serial console only.");
        }

        int rc = ai_chat_raw(ctx->prompt, ctx->reply,
                             SCHED_REPLY_MAX);
        if (rc == CLAW_OK && ctx->reply[0] != '\0') {
            if (ctx->reply_fn) {
                ctx->reply_fn(ctx->reply_target, ctx->reply);
            } else {
                printf("\n\033[0;33m<sched>\033[0m %s\n",
                       ctx->reply);
                fflush(stdout);
            }
        } else {
            /*
             * Task failed.  Save the error reason, then decide
             * whether to ask the agent to compose a friendly
             * notification or just forward the raw error.
             */
            char err_reason[128];
            const char *src = ctx->reply[0] ? ctx->reply
                                            : "unknown error";
            strncpy(err_reason, src, sizeof(err_reason) - 1);
            err_reason[sizeof(err_reason) - 1] = '\0';

            int notified = 0;

            /*
             * Only retry via AI when the failure is NOT an API
             * error — if the API itself is down (503, timeout,
             * etc.) another call will also fail.
             */
            if (!strstr(err_reason, "API")) {
                snprintf(ctx->prompt, SCHED_PROMPT_MAX,
                         "A scheduled task failed: %s. "
                         "Briefly inform the user and suggest "
                         "they can retry later.",
                         err_reason);

                rc = ai_chat_raw(ctx->prompt, ctx->reply,
                                 SCHED_REPLY_MAX);
                if (rc == CLAW_OK && ctx->reply[0] != '\0') {
                    if (ctx->reply_fn) {
                        ctx->reply_fn(ctx->reply_target,
                                      ctx->reply);
                    } else {
                        printf("\n\033[0;33m<sched>\033[0m %s\n",
                               ctx->reply);
                        fflush(stdout);
                    }
                    notified = 1;
                }
            }

            if (!notified) {
                if (ctx->reply_fn) {
                    ctx->reply_fn(ctx->reply_target, err_reason);
                } else {
                    printf("\n\033[0;33m<sched>\033[0m %s\n",
                           err_reason);
                    fflush(stdout);
                }
            }
        }

        ai_set_channel_hint(NULL);

        claw_mutex_lock(s_worker_lock, CLAW_WAIT_FOREVER);
        s_worker_busy = 0;
        claw_mutex_unlock(s_worker_lock);
    }
}

/*
 * Scheduler callback — just posts work to the worker thread.
 * Returns immediately so the scheduler is never blocked.
 */
static void sched_ai_callback(void *arg)
{
    sched_ai_ctx_t *ctx = (sched_ai_ctx_t *)arg;

    claw_mutex_lock(s_worker_lock, CLAW_WAIT_FOREVER);
    if (s_worker_busy) {
        /* Worker still processing previous call, skip */
        claw_mutex_unlock(s_worker_lock);
        CLAW_LOGD(TAG, "worker busy, skipping tick");
        return;
    }
    s_pending_ctx = ctx;
    claw_mutex_unlock(s_worker_lock);

    claw_sem_give(s_worker_sem);
}

/* --- NVS persistence for AI-created scheduled tasks --- */

static int sched_nvs_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(SCHED_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        CLAW_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return CLAW_ERROR;
    }

    /* Count active contexts */
    uint8_t cnt = 0;
    for (int i = 0; i < SCHED_AI_MAX; i++) {
        if (s_ctx[i].in_use) {
            cnt++;
        }
    }

    nvs_set_u8(h, "cnt", cnt);

    int idx = 0;
    for (int i = 0; i < SCHED_AI_MAX; i++) {
        if (!s_ctx[i].in_use) {
            continue;
        }
        sched_persist_t rec;
        memset(&rec, 0, sizeof(rec));
        snprintf(rec.name, sizeof(rec.name), "%s", s_ctx[i].name);
        snprintf(rec.prompt, sizeof(rec.prompt), "%s", s_ctx[i].prompt);
        rec.interval_s = s_ctx[i].interval_s;
        rec.count = s_ctx[i].count;
        snprintf(rec.reply_target, sizeof(rec.reply_target),
                 "%s", s_ctx[i].reply_target);

        char key[8];
        snprintf(key, sizeof(key), "t%d", idx);
        nvs_set_blob(h, key, &rec, sizeof(rec));
        idx++;
    }

    err = nvs_commit(h);
    nvs_close(h);
    return (err == ESP_OK) ? CLAW_OK : CLAW_ERROR;
}

static void sched_nvs_restore(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(SCHED_NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return; /* No saved tasks */
    }

    uint8_t cnt = 0;
    err = nvs_get_u8(h, "cnt", &cnt);
    if (err != ESP_OK || cnt == 0) {
        nvs_close(h);
        return;
    }

    CLAW_LOGI(TAG, "restoring %d persistent task(s) from NVS", cnt);

    for (int i = 0; i < cnt && i < SCHED_AI_MAX; i++) {
        char key[8];
        snprintf(key, sizeof(key), "t%d", i);

        sched_persist_t rec;
        size_t blob_size = sizeof(rec);
        err = nvs_get_blob(h, key, &rec, &blob_size);
        if (err != ESP_OK) {
            continue;
        }

        sched_ai_ctx_t *ctx = ctx_alloc();
        if (!ctx) {
            break;
        }

        snprintf(ctx->name, sizeof(ctx->name), "%s", rec.name);
        snprintf(ctx->prompt, SCHED_PROMPT_MAX, "%s", rec.prompt);
        ctx->reply_fn = NULL;  /* late-bound at execution time */
        snprintf(ctx->reply_target, REPLY_TARGET_MAX,
                 "%s", rec.reply_target);

        /* Restore with original interval or default 60s */
        uint32_t interval_s = rec.interval_s > 0 ? rec.interval_s : 60;

        if (sched_add(rec.name, interval_s * 1000, rec.count,
                      sched_ai_callback, ctx) != CLAW_OK) {
            ctx->in_use = 0;
            CLAW_LOGW(TAG, "failed to restore task '%s'", rec.name);
        } else {
            CLAW_LOGI(TAG, "restored task '%s' (every %us)",
                      rec.name, (unsigned)interval_s);
        }
    }

    nvs_close(h);
}

static int tool_schedule_task(const cJSON *params, cJSON *result)
{
    cJSON *name_j = cJSON_GetObjectItem(params, "name");
    cJSON *interval_j = cJSON_GetObjectItem(params, "interval_seconds");
    cJSON *count_j = cJSON_GetObjectItem(params, "count");
    cJSON *prompt_j = cJSON_GetObjectItem(params, "prompt");

    if (!name_j || !cJSON_IsString(name_j) ||
        !interval_j || !cJSON_IsNumber(interval_j) ||
        !prompt_j || !cJSON_IsString(prompt_j)) {
        cJSON_AddStringToObject(result, "error",
                                "missing name, interval_seconds, or prompt");
        return CLAW_ERROR;
    }

    const char *name = name_j->valuestring;
    int interval_s = interval_j->valueint;
    int count = -1;

    if (count_j && cJSON_IsNumber(count_j)) {
        count = count_j->valueint;
    }

    if (interval_s < 1) {
        cJSON_AddStringToObject(result, "error",
                                "interval_seconds must be >= 1");
        return CLAW_ERROR;
    }

    sched_ai_ctx_t *ctx = ctx_alloc();
    if (!ctx) {
        cJSON_AddStringToObject(result, "error",
                                "max scheduled AI tasks reached");
        return CLAW_ERROR;
    }

    snprintf(ctx->name, sizeof(ctx->name), "%s", name);
    snprintf(ctx->prompt, SCHED_PROMPT_MAX, "%s",
             prompt_j->valuestring);
    ctx->interval_s = (uint32_t)interval_s;
    ctx->count = (int32_t)count;

    /* Capture reply context set by the caller (feishu / shell) */
    claw_mutex_lock(s_rctx_lock, CLAW_WAIT_FOREVER);
    ctx->reply_fn = s_rctx_fn;
    snprintf(ctx->reply_target, REPLY_TARGET_MAX, "%s", s_rctx_target);
    claw_mutex_unlock(s_rctx_lock);

    if (sched_add(name, (uint32_t)interval_s * 1000, (int32_t)count,
                  sched_ai_callback, ctx) != CLAW_OK) {
        ctx->in_use = 0;
        cJSON_AddStringToObject(result, "error",
                                "scheduler full or duplicate name");
        return CLAW_ERROR;
    }

    sched_nvs_save();

    cJSON_AddStringToObject(result, "status", "ok");
    char msg[128];

    snprintf(msg, sizeof(msg),
             "task '%s' scheduled every %ds (count=%d)",
             name, interval_s, count);
    cJSON_AddStringToObject(result, "message", msg);
    return CLAW_OK;
}

static int tool_remove_task(const cJSON *params, cJSON *result)
{
    cJSON *name_j = cJSON_GetObjectItem(params, "name");

    if (!name_j || !cJSON_IsString(name_j)) {
        cJSON_AddStringToObject(result, "error", "missing name");
        return CLAW_ERROR;
    }

    const char *name = name_j->valuestring;

    if (sched_remove(name) != CLAW_OK) {
        cJSON_AddStringToObject(result, "error", "task not found");
        return CLAW_ERROR;
    }

    ctx_free_by_name(name);
    sched_nvs_save();
    cJSON_AddStringToObject(result, "status", "ok");

    char msg[64];

    snprintf(msg, sizeof(msg), "task '%s' removed", name);
    cJSON_AddStringToObject(result, "message", msg);
    return CLAW_OK;
}

static const char schema_schedule[] =
    "{\"type\":\"object\","
    "\"properties\":{"
    "\"name\":{\"type\":\"string\","
    "\"description\":\"Unique task name\"},"
    "\"interval_seconds\":{\"type\":\"integer\","
    "\"description\":\"Interval in seconds between executions\"},"
    "\"count\":{\"type\":\"integer\","
    "\"description\":\"Number of executions (-1 for infinite, default -1)\"},"
    "\"prompt\":{\"type\":\"string\","
    "\"description\":\"AI prompt to execute on each tick\"}},"
    "\"required\":[\"name\",\"interval_seconds\",\"prompt\"]}";

static const char schema_remove[] =
    "{\"type\":\"object\","
    "\"properties\":{"
    "\"name\":{\"type\":\"string\","
    "\"description\":\"Name of the task to remove\"}},"
    "\"required\":[\"name\"]}";

void claw_tools_register_sched(void)
{
    memset(s_ctx, 0, sizeof(s_ctx));
    s_worker_busy = 0;
    s_pending_ctx = NULL;
    s_rctx_fn = NULL;
    s_rctx_target[0] = '\0';

    s_worker_sem = claw_sem_create("sched_w", 0);
    s_worker_lock = claw_mutex_create("sched_w");
    s_rctx_lock = claw_mutex_create("sched_rc");

    claw_thread_create("sched_ai", ai_worker_thread, NULL,
                       WORKER_STACK, WORKER_PRIO);

    claw_tool_register("schedule_task",
        "Schedule a recurring AI task. The prompt will be executed "
        "periodically at the given interval. Use this when the user "
        "asks to do something repeatedly or on a timer.",
        schema_schedule, tool_schedule_task);

    claw_tool_register("remove_task",
        "Remove a previously scheduled recurring task by name.",
        schema_remove, tool_remove_task);

    /* Restore persistent tasks from NVS after boot */
    sched_nvs_restore();
}

#else

void claw_tools_register_sched(void)
{
}

void sched_set_reply_context(sched_reply_fn_t fn, const char *target)
{
    (void)fn;
    (void)target;
}

#endif
