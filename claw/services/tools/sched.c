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
#include "claw/services/tools/tools.h"
#include "claw/services/sched.h"
#include "claw/services/ai/ai_engine.h"

#include <string.h>
#include <stdio.h>

#ifndef CLAW_SCHED_TOOL_STUB

#include "cJSON.h"
#include "osal/claw_kv.h"

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
    char *prompt;               /* heap-allocated, freed on ctx_free */
    int  in_use;
    int  pending;               /* 1 = queued for next worker cycle */
    uint32_t interval_s;        /* for NVS persistence */
    int32_t  count;             /* for NVS persistence */
    sched_reply_fn_t reply_fn;
    char reply_target[REPLY_TARGET_MAX];
} sched_ai_ctx_t;

static sched_ai_ctx_t s_ctx[SCHED_AI_MAX];

/* Worker thread state */
static struct claw_thread *s_ai_worker;
static struct claw_sem *s_worker_sem;
static struct claw_mutex *s_worker_lock;
#ifdef CONFIG_RTCLAW_TOOL_SCHED
static sched_ai_ctx_t *s_pending_ctx;
static int s_worker_busy; /* protected by s_worker_lock */
#endif

/*
 * Reply context — set by the caller (feishu / shell) before ai_chat()
 * so that tool_schedule_task() can capture the destination.
 * Protected by s_rctx_lock; read inside ai_chat() tool execution
 * while s_api_lock is held, so races are practically impossible
 * but we lock anyway for correctness.
 */
static struct claw_mutex *s_rctx_lock;
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

#ifdef CONFIG_RTCLAW_TOOL_SCHED
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

static int sched_nvs_save(void);
#else
static int sched_nvs_save(void) { return 0; }
#endif

static void ctx_free_by_name(const char *name)
{
    for (int i = 0; i < SCHED_AI_MAX; i++) {
        if (s_ctx[i].in_use &&
            strcmp(s_ctx[i].name, name) == 0) {
            claw_free(s_ctx[i].prompt);
            s_ctx[i].prompt = NULL;
            s_ctx[i].in_use = 0;
            return;
        }
    }
}

int sched_tool_remove_by_name(const char *name)
{
    if (!name) {
        return CLAW_ERROR;
    }
    if (sched_remove(name) != CLAW_OK) {
        return CLAW_ERROR;
    }
    ctx_free_by_name(name);
    sched_nvs_save();
    return CLAW_OK;
}

/*
 * Everything below until the matching #endif is the LLM tool
 * subsystem: worker thread, AI callback, NVS persistence, tool
 * execute functions, schemas, init/cleanup, and registration.
 * Only compiled when tool_sched is enabled.
 */
#ifdef CONFIG_RTCLAW_TOOL_SCHED

static int s_rr_idx;

static sched_ai_ctx_t *drain_pending(void)
{
    for (int i = 0; i < SCHED_AI_MAX; i++) {
        int idx = (s_rr_idx + i) % SCHED_AI_MAX;
        if (s_ctx[idx].in_use && s_ctx[idx].pending) {
            s_ctx[idx].pending = 0;
            s_rr_idx = (idx + 1) % SCHED_AI_MAX;
            return &s_ctx[idx];
        }
    }
    return NULL;
}

/* Worker thread — runs AI calls with sufficient stack */
static void ai_worker_thread(void *arg)
{
    (void)arg;

    while (!claw_thread_should_exit()) {
        if (claw_sem_take(s_worker_sem, 1000) != CLAW_OK) {
            continue;
        }

        claw_mutex_lock(s_worker_lock, CLAW_WAIT_FOREVER);
        sched_ai_ctx_t *ctx = s_pending_ctx;
        s_pending_ctx = NULL;
        if (!ctx) {
            ctx = drain_pending();
        }
        s_worker_busy = (ctx != NULL);
        claw_mutex_unlock(s_worker_lock);

        if (!ctx) {
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

        ai_set_channel(AI_CHANNEL_SCHED);
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

        char *reply = claw_malloc(SCHED_REPLY_MAX);
        if (!reply) {
            CLAW_LOGE(TAG, "reply alloc failed");
            ai_set_channel_hint(NULL);
            claw_mutex_lock(s_worker_lock, CLAW_WAIT_FOREVER);
            s_worker_busy = 0;
            claw_mutex_unlock(s_worker_lock);
            continue;
        }

        int rc = ai_chat_raw(ctx->prompt, reply, SCHED_REPLY_MAX);
        if (rc == CLAW_OK && reply[0] != '\0') {
            if (ctx->reply_fn) {
                ctx->reply_fn(ctx->reply_target, reply);
            } else {
                printf("\n\033[0;33msched>\033[0m %s\n", reply);
                fflush(stdout);
            }
        } else {
            char err_reason[128];
            const char *src = reply[0] ? reply : "unknown error";
            strncpy(err_reason, src, sizeof(err_reason) - 1);
            err_reason[sizeof(err_reason) - 1] = '\0';

            int notified = 0;

            if (!strstr(err_reason, "API")) {
                char retry_prompt[SCHED_PROMPT_MAX];
                snprintf(retry_prompt, sizeof(retry_prompt),
                         "A scheduled task failed: %s. "
                         "Briefly inform the user and suggest "
                         "they can retry later.",
                         err_reason);

                rc = ai_chat_raw(retry_prompt, reply,
                                 SCHED_REPLY_MAX);
                if (rc == CLAW_OK && reply[0] != '\0') {
                    if (ctx->reply_fn) {
                        ctx->reply_fn(ctx->reply_target, reply);
                    } else {
                        printf("\n\033[0;33msched>\033[0m %s\n",
                               reply);
                        fflush(stdout);
                    }
                    notified = 1;
                }
            }

            if (!notified) {
                if (ctx->reply_fn) {
                    ctx->reply_fn(ctx->reply_target, err_reason);
                } else {
                    printf("\n\033[0;33msched>\033[0m %s\n",
                           err_reason);
                    fflush(stdout);
                }
            }
        }

        claw_free(reply);
        ai_set_channel(AI_CHANNEL_SHELL);
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
    if (s_worker_busy || s_pending_ctx) {
        /*
         * Worker busy — mark pending instead of dropping.
         * sem_give wakes the worker so it picks this up after
         * finishing the current task (one task per wakeup).
         */
        ctx->pending = 1;
        claw_mutex_unlock(s_worker_lock);
        claw_sem_give(s_worker_sem);
        CLAW_LOGD(TAG, "worker busy, '%s' queued", ctx->name);
        return;
    }
    s_pending_ctx = ctx;
    claw_mutex_unlock(s_worker_lock);

    claw_sem_give(s_worker_sem);
}

/* --- KV persistence for AI-created scheduled tasks --- */

static int sched_nvs_save(void)
{
    uint8_t cnt = 0;
    for (int i = 0; i < SCHED_AI_MAX; i++) {
        if (s_ctx[i].in_use) {
            cnt++;
        }
    }

    claw_kv_set_u8(SCHED_NVS_NS, "cnt", cnt);

    int idx = 0;
    for (int i = 0; i < SCHED_AI_MAX; i++) {
        if (!s_ctx[i].in_use) {
            continue;
        }
        sched_persist_t rec;
        memset(&rec, 0, sizeof(rec));
        snprintf(rec.name, sizeof(rec.name), "%s",
                 s_ctx[i].name);
        if (s_ctx[i].prompt) {
            snprintf(rec.prompt, sizeof(rec.prompt), "%s",
                     s_ctx[i].prompt);
        } else {
            rec.prompt[0] = '\0';
        }
        rec.interval_s = s_ctx[i].interval_s;
        rec.count = s_ctx[i].count;
        snprintf(rec.reply_target,
                 sizeof(rec.reply_target),
                 "%s", s_ctx[i].reply_target);

        char key[8];
        snprintf(key, sizeof(key), "t%d", idx);
        claw_kv_set_blob(SCHED_NVS_NS, key,
                         &rec, sizeof(rec));
        idx++;
    }

    return CLAW_OK;
}

static void sched_nvs_restore(void)
{
    uint8_t cnt = 0;
    if (claw_kv_get_u8(SCHED_NVS_NS, "cnt", &cnt) != CLAW_OK
        || cnt == 0) {
        return;
    }

    CLAW_LOGI(TAG, "restoring %d persistent task(s)", cnt);

    for (int i = 0; i < cnt && i < SCHED_AI_MAX; i++) {
        char key[8];
        snprintf(key, sizeof(key), "t%d", i);

        sched_persist_t rec;
        size_t blob_size = sizeof(rec);
        if (claw_kv_get_blob(SCHED_NVS_NS, key,
                             &rec, &blob_size) != CLAW_OK) {
            continue;
        }

        sched_ai_ctx_t *ctx = ctx_alloc();
        if (!ctx) {
            break;
        }

        snprintf(ctx->name, sizeof(ctx->name),
                 "%s", rec.name);
        ctx->prompt = claw_malloc(strlen(rec.prompt) + 1);
        if (!ctx->prompt) {
            ctx->in_use = 0;
            break;
        }
        strcpy(ctx->prompt, rec.prompt);
        ctx->reply_fn = NULL;
        snprintf(ctx->reply_target, REPLY_TARGET_MAX,
                 "%s", rec.reply_target);

        uint32_t interval_s =
            rec.interval_s > 0 ? rec.interval_s : 60;
        ctx->interval_s = interval_s;
        ctx->count = rec.count;

        if (sched_add(rec.name, interval_s * 1000,
                      rec.count,
                      sched_ai_callback, ctx) != CLAW_OK) {
            ctx->in_use = 0;
            CLAW_LOGW(TAG, "failed to restore '%s'",
                      rec.name);
        } else {
            CLAW_LOGI(TAG, "restored '%s' (every %us)",
                      rec.name, (unsigned)interval_s);
        }
    }
}

static claw_err_t tool_schedule_task(struct claw_tool *tool,
                                     const cJSON *params, cJSON *result)
{
    (void)tool;
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
    if (name[0] == '\0') {
        cJSON_AddStringToObject(result, "error",
                                "task name must not be empty");
        return CLAW_ERROR;
    }
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
    size_t plen = strlen(prompt_j->valuestring);
    if (plen >= SCHED_PROMPT_MAX) {
        plen = SCHED_PROMPT_MAX - 1;
    }
    ctx->prompt = claw_malloc(plen + 1);
    if (!ctx->prompt) {
        ctx->in_use = 0;
        cJSON_AddStringToObject(result, "error", "out of memory");
        return CLAW_ERROR;
    }
    memcpy(ctx->prompt, prompt_j->valuestring, plen);
    ctx->prompt[plen] = '\0';
    ctx->interval_s = (uint32_t)interval_s;
    ctx->count = (int32_t)count;

    /* Capture reply context set by the caller (feishu / shell) */
    claw_mutex_lock(s_rctx_lock, CLAW_WAIT_FOREVER);
    ctx->reply_fn = s_rctx_fn;
    snprintf(ctx->reply_target, REPLY_TARGET_MAX, "%s", s_rctx_target);
    claw_mutex_unlock(s_rctx_lock);

    if (sched_add(name, (uint32_t)interval_s * 1000, (int32_t)count,
                  sched_ai_callback, ctx) != CLAW_OK) {
        claw_free(ctx->prompt);
        ctx->prompt = NULL;
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

static claw_err_t tool_remove_task(struct claw_tool *tool,
                                   const cJSON *params, cJSON *result)
{
    (void)tool;
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

static claw_err_t tool_list_tasks(struct claw_tool *tool,
                                  const cJSON *params, cJSON *result)
{
    (void)tool;
    (void)params;
    char buf[512];
    sched_list_to_buf(buf, sizeof(buf));
    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddStringToObject(result, "tasks", buf);
    cJSON_AddNumberToObject(result, "count", sched_task_count());
    return CLAW_OK;
}

static const char schema_list_tasks[] =
    "{\"type\":\"object\",\"properties\":{}}";

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

static int s_sched_inited;

static claw_err_t sched_tool_init_subsys(struct claw_tool *tool)
{
    (void)tool;
    if (s_sched_inited) {
        return CLAW_OK;
    }

    memset(s_ctx, 0, sizeof(s_ctx));
    s_worker_busy = 0;
    s_pending_ctx = NULL;
    s_rctx_fn = NULL;
    s_rctx_target[0] = '\0';

    s_worker_sem = claw_sem_create("sched_w", 0);
    if (!s_worker_sem) {
        return CLAW_ERR_NOMEM;
    }

    s_worker_lock = claw_mutex_create("sched_w");
    if (!s_worker_lock) {
        claw_sem_delete(s_worker_sem);
        s_worker_sem = NULL;
        return CLAW_ERR_NOMEM;
    }

    s_rctx_lock = claw_mutex_create("sched_rc");
    if (!s_rctx_lock) {
        claw_mutex_delete(s_worker_lock);
        s_worker_lock = NULL;
        claw_sem_delete(s_worker_sem);
        s_worker_sem = NULL;
        return CLAW_ERR_NOMEM;
    }

    s_ai_worker = claw_thread_create("sched_ai",
        ai_worker_thread, NULL,
        WORKER_STACK, WORKER_PRIO);
    if (!s_ai_worker) {
        claw_mutex_delete(s_rctx_lock);
        s_rctx_lock = NULL;
        claw_mutex_delete(s_worker_lock);
        s_worker_lock = NULL;
        claw_sem_delete(s_worker_sem);
        s_worker_sem = NULL;
        return CLAW_ERR_NOMEM;
    }

    s_sched_inited = 1;
    sched_nvs_restore();
    return CLAW_OK;
}

static void sched_tool_cleanup(struct claw_tool *tool)
{
    (void)tool;
    if (!s_sched_inited) {
        return;
    }
    sched_tool_stop();
    s_sched_inited = 0;
}

/* OOP tool registration */
static const struct claw_tool_ops list_tasks_ops = {
    .execute = tool_list_tasks,
    .init = sched_tool_init_subsys,
    .cleanup = sched_tool_cleanup,
};
static struct claw_tool list_tasks_tool = {
    .name = "list_tasks",
    .description =
        "List all active scheduled tasks with their names, "
        "intervals, and remaining execution counts. Call this "
        "before removing tasks to see what's available.",
    .input_schema_json = schema_list_tasks,
    .ops = &list_tasks_ops,
    .flags = CLAW_TOOL_LOCAL_ONLY,
};
CLAW_TOOL_REGISTER(list_tasks, &list_tasks_tool);

static const struct claw_tool_ops schedule_task_ops = {
    .execute = tool_schedule_task,
};
static struct claw_tool schedule_task_tool = {
    .name = "schedule_task",
    .description =
        "Schedule a recurring AI task. The prompt will be executed "
        "periodically at the given interval. Use this when the user "
        "asks to do something repeatedly or on a timer.",
    .input_schema_json = schema_schedule,
    .ops = &schedule_task_ops,
    .flags = CLAW_TOOL_LOCAL_ONLY,
};
CLAW_TOOL_REGISTER(schedule_task, &schedule_task_tool);

static const struct claw_tool_ops remove_task_ops = {
    .execute = tool_remove_task,
};
static struct claw_tool remove_task_tool = {
    .name = "remove_task",
    .description =
        "Remove a previously scheduled recurring task by name.",
    .input_schema_json = schema_remove,
    .ops = &remove_task_ops,
    .flags = CLAW_TOOL_LOCAL_ONLY,
};
CLAW_TOOL_REGISTER(remove_task, &remove_task_tool);

#endif /* CONFIG_RTCLAW_TOOL_SCHED */

void sched_tool_stop(void)
{
    /* Remove all registered tasks so no new callbacks fire */
    for (int i = 0; i < SCHED_AI_MAX; i++) {
        if (s_ctx[i].in_use) {
            sched_remove(s_ctx[i].name);
        }
    }

    /* Wake worker so it can check exit flag and return */
    if (s_worker_sem) {
        claw_sem_give(s_worker_sem);
    }

    if (s_ai_worker) {
        claw_thread_delete(s_ai_worker);
        s_ai_worker = NULL;
    }
    if (s_worker_sem) {
        claw_sem_delete(s_worker_sem);
        s_worker_sem = NULL;
    }
    if (s_worker_lock) {
        claw_mutex_delete(s_worker_lock);
        s_worker_lock = NULL;
    }
    if (s_rctx_lock) {
        claw_mutex_delete(s_rctx_lock);
        s_rctx_lock = NULL;
    }
}

#else /* !CLAW_PLATFORM_ESP_IDF */

void sched_set_reply_context(sched_reply_fn_t fn, const char *target)
{
    (void)fn;
    (void)target;
}

int sched_tool_remove_by_name(const char *name)
{
    (void)name;
    return -1;
}

void sched_tool_stop(void) {}

#endif
