/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Telegram Bot IM integration via HTTP long polling.
 * OOP: private context struct embedding struct claw_service.
 *
 * Protocol:
 *   1. getUpdates with timeout=30 to long-poll for new messages
 *   2. Forward user messages to ai_chat(), reply via sendMessage
 *   3. update_id offset provides natural dedup (no dedup table needed)
 *
 * Three worker threads:
 *   - tg_poll:     getUpdates loop -> inbound queue
 *   - tg_ai:       inbound queue -> ai_chat() -> outbound queue
 *   - tg_out:      outbound queue -> sendMessage API
 */

#include "osal/claw_os.h"
#include "claw/core/claw_service.h"
#include "utils/list.h"
#include "claw_config.h"
#include "claw/services/im/telegram.h"
#include "claw/services/im/im_util.h"
#include "claw/services/ai/ai_engine.h"
#include "claw/services/ai/ai_skill.h"
#include "claw/shell/shell_cmd.h"
#include "claw/tools/claw_tools.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#define TAG "telegram"

#ifdef CONFIG_RTCLAW_TELEGRAM_ENABLE

#include "osal/claw_net.h"
#include "cJSON.h"

/* ------------------------------------------------------------------ */
/*  Constants                                                          */
/* ------------------------------------------------------------------ */

#define BOT_TOKEN_MAX       128
#define API_BASE_MAX        192
#define RESP_BUF_SIZE       4096
#define REPLY_BUF_SIZE      4096
#define POLL_TIMEOUT_SEC    30
#define HTTP_POLL_TIMEOUT_MS ((POLL_TIMEOUT_SEC + 5) * 1000)
#define HTTP_SEND_TIMEOUT_MS 30000
#define HTTP_ACTION_TIMEOUT_MS 15000
#define TG_MAX_MSG_LEN      4096
#define POLL_RETRY_MS       5000
#define POLL_STACK          8192
#define WORKER_STACK        16384
#define OUTBOUND_STACK      8192
#define INBOUND_DEPTH       4
#define OUTBOUND_DEPTH      4
#define MSG_TEXT_MAX         1024
#define CHAT_ID_MAX         24

/* ------------------------------------------------------------------ */
/*  Message structures                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    char text[MSG_TEXT_MAX];
    char chat_id[CHAT_ID_MAX];
} tg_inbound_t;

typedef struct {
    char chat_id[CHAT_ID_MAX];
    char *text;     /* heap-allocated, consumer frees */
} tg_outbound_t;

/* ------------------------------------------------------------------ */
/*  Service context — all state lives here, no file-scope globals      */
/* ------------------------------------------------------------------ */

struct telegram_ctx {
    struct claw_service    base;           /* MUST be first member */

    char                   bot_token[BOT_TOKEN_MAX];
    char                   api_base[API_BASE_MAX];
    int64_t                update_offset;

    struct claw_mq        *inbound_q;
    struct claw_mq        *outbound_q;

    struct claw_thread    *ai_thread;
    struct claw_thread    *out_thread;
    struct claw_thread    *poll_thread;
};

CLAW_ASSERT_EMBEDDED_FIRST(struct telegram_ctx, base);

/* Singleton — tentative definition; full initializer at file end */
static struct telegram_ctx s_tg;

/* ------------------------------------------------------------------ */
/*  HTTP helper (platform-independent via OSAL)                        */
/* ------------------------------------------------------------------ */

static int tg_api_post(struct telegram_ctx *ctx, const char *method,
                       const char *body, char *resp, size_t resp_size,
                       int timeout_ms)
{
    (void)timeout_ms;
    char url[256];
    snprintf(url, sizeof(url), "%s/%s", ctx->api_base, method);

    claw_net_header_t hdrs[1];
    hdrs[0].key = "Content-Type";
    hdrs[0].value = "application/json";

    size_t resp_len = 0;
    int status = claw_net_post(url, hdrs, 1,
                               body, strlen(body),
                               resp, resp_size, &resp_len);

    if (status < 200 || status >= 300) {
        CLAW_LOGE(TAG, "POST %s failed: status=%d",
                  method, status);
        return CLAW_ERROR;
    }
    return CLAW_OK;
}

/* ------------------------------------------------------------------ */
/*  Telegram API wrappers                                              */
/* ------------------------------------------------------------------ */

/* Send "typing" chat action indicator */
static void send_chat_action(struct telegram_ctx *ctx, const char *chat_id)
{
    char body[128];
    snprintf(body, sizeof(body),
             "{\"chat_id\":%s,\"action\":\"typing\"}", chat_id);

    char resp[256];
    tg_api_post(ctx, "sendChatAction", body, resp, sizeof(resp),
                HTTP_ACTION_TIMEOUT_MS);
}

/*
 * Send a single message (up to TG_MAX_MSG_LEN).
 * Telegram supports MarkdownV2 but it's strict with escaping,
 * so we use plain text for reliability on embedded.
 */
static int send_one_message(struct telegram_ctx *ctx, const char *chat_id,
                            const char *text)
{
    cJSON *body = cJSON_CreateObject();
    cJSON_AddRawToObject(body, "chat_id", chat_id);
    cJSON_AddStringToObject(body, "text", text);

    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!body_str) {
        return CLAW_NOMEM;
    }

    char *resp = claw_malloc(1024);
    if (!resp) {
        claw_free(body_str);
        return CLAW_NOMEM;
    }
    resp[0] = '\0';

    int ret = tg_api_post(ctx, "sendMessage", body_str, resp, 1024,
                           HTTP_SEND_TIMEOUT_MS);
    if (ret != CLAW_OK) {
        CLAW_LOGE(TAG, "sendMessage failed: %.200s", resp);
    }

    claw_free(body_str);
    claw_free(resp);
    return ret;
}

/*
 * Send reply with auto-chunking for messages > TG_MAX_MSG_LEN.
 * Split at the last newline before the limit.
 */
static int send_reply(struct telegram_ctx *ctx, const char *chat_id,
                      const char *text)
{
    size_t total = strlen(text);
    if (total <= TG_MAX_MSG_LEN) {
        return send_one_message(ctx, chat_id, text);
    }

    const char *p = text;
    size_t remaining = total;
    int part = 1;

    while (remaining > 0) {
        size_t chunk = im_find_chunk_end(p, remaining,
                                         TG_MAX_MSG_LEN);

        char *chunk_buf = claw_malloc(chunk + 1);
        if (!chunk_buf) {
            return CLAW_NOMEM;
        }
        memcpy(chunk_buf, p, chunk);
        chunk_buf[chunk] = '\0';

        CLAW_LOGI(TAG, "send part %d (%d bytes)", part, (int)chunk);
        int ret = send_one_message(ctx, chat_id, chunk_buf);
        claw_free(chunk_buf);

        if (ret != CLAW_OK) {
            return ret;
        }

        p += chunk;
        remaining -= chunk;
        part++;
    }
    return CLAW_OK;
}

/* Forward declaration */
static void enqueue_reply(struct telegram_ctx *ctx, const char *chat_id,
                          const char *text);

/* ------------------------------------------------------------------ */
/*  Scheduled-task reply callback                                      */
/* ------------------------------------------------------------------ */

static void sched_reply_to_telegram(const char *target, const char *text)
{
    if (target && target[0] != '\0' && text && text[0] != '\0') {
        enqueue_reply(&s_tg, target, text);
    }
}

/* ------------------------------------------------------------------ */
/*  Outbound thread — sends replies via HTTP                           */
/* ------------------------------------------------------------------ */

static void tg_outbound_thread(void *arg)
{
    struct telegram_ctx *ctx = (struct telegram_ctx *)arg;
    tg_outbound_t msg;

    while (!claw_thread_should_exit()) {
        if (claw_mq_recv(ctx->outbound_q, &msg, sizeof(msg),
                         1000) != CLAW_OK) {
            continue;
        }
        if (!msg.text) {
            continue;
        }

        CLAW_LOGI(TAG, "[%lu ms] >>> sending: \"%.80s%s\"",
                  (unsigned long)claw_tick_ms(), msg.text,
                  strlen(msg.text) > 80 ? "..." : "");
        send_reply(ctx, msg.chat_id, msg.text);
        CLAW_LOGI(TAG, "[%lu ms] >>> sent (heap=%u)",
                  (unsigned long)claw_tick_ms(),
                  (unsigned)claw_tick_ms());
        claw_free(msg.text);
    }
}

static void enqueue_reply(struct telegram_ctx *ctx, const char *chat_id,
                          const char *text)
{
    size_t len = strlen(text);
    char *copy = claw_malloc(len + 1);
    if (!copy) {
        CLAW_LOGE(TAG, "outbound: no memory for reply");
        return;
    }
    memcpy(copy, text, len + 1);

    tg_outbound_t msg;
    snprintf(msg.chat_id, sizeof(msg.chat_id), "%s", chat_id);
    msg.text = copy;

    if (claw_mq_send(ctx->outbound_q, &msg, sizeof(msg), 1000)
            != CLAW_OK) {
        CLAW_LOGW(TAG, "outbound queue full, dropping reply");
        claw_free(copy);
    }
}

/* ------------------------------------------------------------------ */
/*  AI worker thread — inbound queue -> ai_chat -> outbound queue      */
/* ------------------------------------------------------------------ */

static void tg_ai_worker(void *arg)
{
    struct telegram_ctx *ctx = (struct telegram_ctx *)arg;
    char *reply = claw_malloc(REPLY_BUF_SIZE);
    if (!reply) {
        CLAW_LOGE(TAG, "worker: no memory");
        return;
    }

    tg_inbound_t in;

    while (!claw_thread_should_exit()) {
        if (claw_mq_recv(ctx->inbound_q, &in, sizeof(in),
                         1000) != CLAW_OK) {
            continue;
        }

        /* Intercept /commands — try skill dispatch first */
#ifdef CONFIG_RTCLAW_SKILL_ENABLE
        if (in.text[0] == '/') {
            char line_copy[MSG_TEXT_MAX];
            snprintf(line_copy, sizeof(line_copy), "%s", in.text);
            char *argv[8];
            int argc = shell_tokenize(line_copy, argv, 8);
            if (argc > 0) {
                char *cmd_reply = claw_malloc(REPLY_BUF_SIZE);
                if (cmd_reply &&
                    ai_skill_try_command(argv[0], argc, argv,
                                         cmd_reply,
                                         REPLY_BUF_SIZE) == CLAW_OK) {
                    enqueue_reply(ctx, in.chat_id, cmd_reply);
                    claw_free(cmd_reply);
                    continue;
                }
                claw_free(cmd_reply);
            }
            /* Not a skill — fall through to ai_chat() */
        }
#endif

        /* Typing indicator before AI call */
        send_chat_action(ctx, in.chat_id);

        ai_set_channel(AI_CHANNEL_TELEGRAM);
        ai_set_channel_hint(
            " You are communicating via Telegram."
            " All outputs (including scheduled task results)"
            " will be delivered to this Telegram conversation."
            " Do NOT mention LCD or serial console unless"
            " the user explicitly asks."
            " You may use Markdown formatting (bold, italic,"
            " code blocks) as Telegram supports it.");
        sched_set_reply_context(sched_reply_to_telegram, in.chat_id);

        CLAW_LOGI(TAG, "[%lu ms] ai_chat: \"%s\" (heap=%u)",
                  (unsigned long)claw_tick_ms(), in.text,
                  (unsigned)claw_tick_ms());
        int ret = ai_chat(in.text, reply, REPLY_BUF_SIZE);
        CLAW_LOGI(TAG, "[%lu ms] ai_chat ret=%d (heap=%u)",
                  (unsigned long)claw_tick_ms(), ret,
                  (unsigned)claw_tick_ms());

        ai_set_channel(AI_CHANNEL_SHELL);
        ai_set_channel_hint(NULL);
        sched_set_reply_context(NULL, NULL);

        if (ret == CLAW_OK && reply[0] != '\0') {
            enqueue_reply(ctx, in.chat_id, reply);
        } else {
            if (reply[0] != '\0') {
                enqueue_reply(ctx, in.chat_id, reply);
            } else {
                enqueue_reply(ctx, in.chat_id,
                              "[rt-claw] AI engine error");
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Poll thread — getUpdates long polling                              */
/* ------------------------------------------------------------------ */

/*
 * Parse a single Update object from getUpdates response.
 * Extract chat_id (int64) and text from message.
 */
static void process_update(struct telegram_ctx *ctx, cJSON *update)
{
    cJSON *uid = cJSON_GetObjectItem(update, "update_id");
    if (!uid || !cJSON_IsNumber(uid)) {
        return;
    }

    /* Advance offset past this update */
    int64_t id = (int64_t)uid->valuedouble;
    if (id >= ctx->update_offset) {
        ctx->update_offset = id + 1;
    }

    cJSON *message = cJSON_GetObjectItem(update, "message");
    if (!message) {
        return;
    }

    cJSON *text = cJSON_GetObjectItem(message, "text");
    if (!text || !cJSON_IsString(text)) {
        CLAW_LOGD(TAG, "ignore non-text message");
        return;
    }

    cJSON *chat = cJSON_GetObjectItem(message, "chat");
    if (!chat) {
        return;
    }
    cJSON *chat_id = cJSON_GetObjectItem(chat, "id");
    if (!chat_id || !cJSON_IsNumber(chat_id)) {
        return;
    }

    /* Convert chat_id (int64) to string for queue */
    char chat_id_str[CHAT_ID_MAX];
    snprintf(chat_id_str, sizeof(chat_id_str), "%.0f",
             chat_id->valuedouble);

    CLAW_LOGI(TAG, "[%lu ms] <<< recv chat=%s text=\"%s\"",
              (unsigned long)claw_tick_ms(), chat_id_str,
              text->valuestring);

    tg_inbound_t in;
    snprintf(in.text, sizeof(in.text), "%s", text->valuestring);
    snprintf(in.chat_id, sizeof(in.chat_id), "%s", chat_id_str);

    if (claw_mq_send(ctx->inbound_q, &in, sizeof(in), 0) != CLAW_OK) {
        CLAW_LOGW(TAG, "[%lu ms] !!! inbound queue full, dropping",
                  (unsigned long)claw_tick_ms());
    } else {
        CLAW_LOGI(TAG, "[%lu ms] --> queued",
                  (unsigned long)claw_tick_ms());
    }
}

static void tg_poll_thread(void *arg)
{
    struct telegram_ctx *ctx = (struct telegram_ctx *)arg;

    CLAW_LOGI(TAG, "poll thread starting");

    char *resp = claw_malloc(RESP_BUF_SIZE);
    if (!resp) {
        CLAW_LOGE(TAG, "poll: no memory for response buffer");
        return;
    }

    while (!claw_thread_should_exit()) {
        char body[128];
        snprintf(body, sizeof(body),
                 "{\"offset\":%" PRId64 ",\"timeout\":%d}",
                 ctx->update_offset, POLL_TIMEOUT_SEC);

        resp[0] = '\0';
        int ret = tg_api_post(ctx, "getUpdates", body, resp,
                               RESP_BUF_SIZE, HTTP_POLL_TIMEOUT_MS);
        if (ret != CLAW_OK) {
            CLAW_LOGW(TAG, "getUpdates failed, retry in %dms",
                      POLL_RETRY_MS);
            claw_thread_delay_ms(POLL_RETRY_MS);
            continue;
        }

        cJSON *root = cJSON_Parse(resp);
        if (!root) {
            CLAW_LOGW(TAG, "getUpdates JSON parse failed");
            claw_thread_delay_ms(POLL_RETRY_MS);
            continue;
        }

        cJSON *ok = cJSON_GetObjectItem(root, "ok");
        if (!ok || !cJSON_IsTrue(ok)) {
            CLAW_LOGW(TAG, "getUpdates ok=false: %.200s", resp);
            cJSON_Delete(root);
            claw_thread_delay_ms(POLL_RETRY_MS);
            continue;
        }

        cJSON *result = cJSON_GetObjectItem(root, "result");
        if (result && cJSON_IsArray(result)) {
            int count = cJSON_GetArraySize(result);
            for (int i = 0; i < count; i++) {
                process_update(ctx, cJSON_GetArrayItem(result, i));
            }
        }

        cJSON_Delete(root);
    }
}

/* ------------------------------------------------------------------ */
/*  OOP lifecycle ops                                                  */
/* ------------------------------------------------------------------ */

static claw_err_t telegram_svc_init(struct claw_service *svc)
{
    struct telegram_ctx *ctx = container_of(svc, struct telegram_ctx,
                                            base);

    if (ctx->bot_token[0] == '\0') {
        snprintf(ctx->bot_token, sizeof(ctx->bot_token),
                 "%s", CONFIG_RTCLAW_TELEGRAM_BOT_TOKEN);
    }

    if (ctx->bot_token[0] == '\0') {
        CLAW_LOGE(TAG, "no bot token configured");
        return CLAW_ERR_GENERIC;
    }

    /* Build API base URL: <api_url>/bot<token> */
    snprintf(ctx->api_base, sizeof(ctx->api_base),
             "%s/bot%s", CONFIG_RTCLAW_TELEGRAM_API_URL,
             ctx->bot_token);

    ctx->update_offset = 0;

    ctx->inbound_q = claw_mq_create("tg_in", sizeof(tg_inbound_t),
                                     INBOUND_DEPTH);
    ctx->outbound_q = claw_mq_create("tg_out", sizeof(tg_outbound_t),
                                      OUTBOUND_DEPTH);
    if (!ctx->inbound_q || !ctx->outbound_q) {
        CLAW_LOGE(TAG, "message queue create failed");
        return CLAW_ERR_NOMEM;
    }

    CLAW_LOGI(TAG, "init ok");
    return CLAW_OK;
}

static claw_err_t telegram_svc_start(struct claw_service *svc)
{
    struct telegram_ctx *ctx = container_of(svc, struct telegram_ctx,
                                            base);

    ctx->ai_thread = claw_thread_create("tg_ai", tg_ai_worker,
                                         ctx, WORKER_STACK, 10);
    if (!ctx->ai_thread) {
        CLAW_LOGE(TAG, "failed to create AI worker");
        return CLAW_ERR_GENERIC;
    }

    ctx->out_thread = claw_thread_create("tg_out", tg_outbound_thread,
                                          ctx, OUTBOUND_STACK, 10);
    if (!ctx->out_thread) {
        CLAW_LOGE(TAG, "failed to create outbound thread");
        return CLAW_ERR_GENERIC;
    }

    ctx->poll_thread = claw_thread_create("tg_poll", tg_poll_thread,
                                           ctx, POLL_STACK, 10);
    if (!ctx->poll_thread) {
        CLAW_LOGE(TAG, "failed to create poll thread");
        return CLAW_ERR_GENERIC;
    }

    CLAW_LOGI(TAG, "started (3 threads)");
    return CLAW_OK;
}

static void telegram_svc_stop(struct claw_service *svc)
{
    struct telegram_ctx *ctx = container_of(svc, struct telegram_ctx,
                                            base);

    claw_thread_delete(ctx->poll_thread);
    ctx->poll_thread = NULL;

    claw_thread_delete(ctx->ai_thread);
    ctx->ai_thread = NULL;

    claw_thread_delete(ctx->out_thread);
    ctx->out_thread = NULL;

    if (ctx->inbound_q) {
        claw_mq_delete(ctx->inbound_q);
        ctx->inbound_q = NULL;
    }
    if (ctx->outbound_q) {
        claw_mq_delete(ctx->outbound_q);
        ctx->outbound_q = NULL;
    }

    CLAW_LOGI(TAG, "stopped");
}

/* ------------------------------------------------------------------ */
/*  Public API (delegates to singleton)                                */
/* ------------------------------------------------------------------ */

void telegram_set_bot_token(const char *token)
{
    if (token) {
        snprintf(s_tg.bot_token, sizeof(s_tg.bot_token), "%s", token);
    }
}

const char *telegram_get_bot_token(void)
{
    return s_tg.bot_token;
}

int telegram_init(void)
{
    return telegram_svc_init(&s_tg.base) == CLAW_OK
           ? CLAW_OK : CLAW_ERROR;
}

int telegram_start(void)
{
    return telegram_svc_start(&s_tg.base) == CLAW_OK
           ? CLAW_OK : CLAW_ERROR;
}

void telegram_stop(void)
{
    telegram_svc_stop(&s_tg.base);
}

#else /* !CONFIG_RTCLAW_TELEGRAM_ENABLE */

int  telegram_init(void)  { return 0; }
int  telegram_start(void) { return 0; }
void telegram_stop(void)  {}
void telegram_set_bot_token(const char *t) { (void)t; }
const char *telegram_get_bot_token(void)   { return ""; }

#endif /* CONFIG_RTCLAW_TELEGRAM_ENABLE */

/* ------------------------------------------------------------------ */
/*  OOP service registration                                           */
/* ------------------------------------------------------------------ */

#ifdef CONFIG_RTCLAW_TELEGRAM_ENABLE

static const char *telegram_deps[] = { "ai_engine", "tools", NULL };

static const struct claw_service_ops telegram_svc_ops = {
    .init  = telegram_svc_init,
    .start = telegram_svc_start,
    .stop  = telegram_svc_stop,
};

static struct telegram_ctx s_tg = {
    .base = {
        .name  = "telegram",
        .ops   = &telegram_svc_ops,
        .deps  = telegram_deps,
        .state = CLAW_SVC_CREATED,
    },
};

CLAW_SERVICE_REGISTER(telegram, &s_tg.base);

#endif /* CONFIG_RTCLAW_TELEGRAM_ENABLE */
