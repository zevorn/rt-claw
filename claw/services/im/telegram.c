/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Telegram Bot IM integration via HTTP long polling.
 *
 * Protocol:
 *   1. getUpdates with timeout=30 to long-poll for new messages
 *   2. Forward user messages to ai_chat(), reply via sendMessage
 *   3. update_id offset provides natural dedup (no dedup table needed)
 *
 * Three worker threads:
 *   - tg_poll:     getUpdates loop → inbound queue
 *   - tg_ai:       inbound queue → ai_chat() → outbound queue
 *   - tg_out:      outbound queue → sendMessage API
 */

#include "osal/claw_os.h"
#include "claw_config.h"
#include "claw/services/im/telegram.h"
#include "claw/services/ai/ai_engine.h"
#include "claw/tools/claw_tools.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#define TAG "telegram"

#ifdef CLAW_PLATFORM_ESP_IDF

#include "sdkconfig.h"

#ifdef CONFIG_RTCLAW_TELEGRAM_ENABLE

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"
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
/*  State                                                              */
/* ------------------------------------------------------------------ */

static char s_bot_token[BOT_TOKEN_MAX];
static char s_api_base[API_BASE_MAX];
static int64_t s_update_offset;
static claw_mq_t s_inbound_q;
static claw_mq_t s_outbound_q;

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
/*  HTTP helper                                                        */
/* ------------------------------------------------------------------ */

struct http_ctx {
    char   *buf;
    size_t  len;
    size_t  cap;
};

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    struct http_ctx *ctx = evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx) {
        if (ctx->len + evt->data_len < ctx->cap) {
            memcpy(ctx->buf + ctx->len, evt->data, evt->data_len);
            ctx->len += evt->data_len;
            ctx->buf[ctx->len] = '\0';
        }
    }
    return ESP_OK;
}

/*
 * POST JSON to a Telegram Bot API method.
 * URL = s_api_base + "/" + method  (e.g. ".../sendMessage")
 */
static int tg_api_post(const char *method, const char *body,
                        char *resp, size_t resp_size, int timeout_ms)
{
    char url[256];
    snprintf(url, sizeof(url), "%s/%s", s_api_base, method);

    struct http_ctx ctx = { .buf = resp, .len = 0, .cap = resp_size };

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = timeout_ms,
        .event_handler = on_http_event,
        .user_data = &ctx,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return CLAW_ERROR;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status < 200 || status >= 300) {
        CLAW_LOGE(TAG, "POST %s failed: err=%d status=%d", method, err,
                  status);
        return CLAW_ERROR;
    }
    return CLAW_OK;
}

/* ------------------------------------------------------------------ */
/*  Telegram API wrappers                                              */
/* ------------------------------------------------------------------ */

/* Send "typing" chat action indicator */
static void send_chat_action(const char *chat_id)
{
    char body[128];
    snprintf(body, sizeof(body),
             "{\"chat_id\":%s,\"action\":\"typing\"}", chat_id);

    char resp[256];
    tg_api_post("sendChatAction", body, resp, sizeof(resp),
                HTTP_ACTION_TIMEOUT_MS);
}

/*
 * Send a single message (up to TG_MAX_MSG_LEN).
 * Telegram supports MarkdownV2 but it's strict with escaping,
 * so we use plain text for reliability on embedded.
 */
static int send_one_message(const char *chat_id, const char *text)
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

    int ret = tg_api_post("sendMessage", body_str, resp, 1024,
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
static int send_reply(const char *chat_id, const char *text)
{
    size_t total = strlen(text);
    if (total <= TG_MAX_MSG_LEN) {
        return send_one_message(chat_id, text);
    }

    const char *p = text;
    size_t remaining = total;
    int part = 1;

    while (remaining > 0) {
        size_t chunk = remaining;
        if (chunk > TG_MAX_MSG_LEN) {
            chunk = TG_MAX_MSG_LEN;
            for (size_t i = chunk; i > chunk / 2; i--) {
                if (p[i] == '\n') {
                    chunk = i + 1;
                    break;
                }
            }
        }

        char saved = p[chunk];
        ((char *)p)[chunk] = '\0';
        CLAW_LOGI(TAG, "send part %d (%d bytes)", part, (int)chunk);
        int ret = send_one_message(chat_id, p);
        ((char *)p)[chunk] = saved;

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
static void enqueue_reply(const char *chat_id, const char *text);

/* ------------------------------------------------------------------ */
/*  Scheduled-task reply callback                                      */
/* ------------------------------------------------------------------ */

static void sched_reply_to_telegram(const char *target, const char *text)
{
    if (target && target[0] != '\0' && text && text[0] != '\0') {
        enqueue_reply(target, text);
    }
}

/* ------------------------------------------------------------------ */
/*  Outbound thread — sends replies via HTTP                           */
/* ------------------------------------------------------------------ */

static void tg_outbound_thread(void *arg)
{
    (void)arg;
    tg_outbound_t msg;

    while (1) {
        if (claw_mq_recv(s_outbound_q, &msg, sizeof(msg),
                         CLAW_WAIT_FOREVER) != CLAW_OK) {
            continue;
        }
        if (!msg.text) {
            continue;
        }

        CLAW_LOGI(TAG, "[%lu ms] >>> sending: \"%.80s%s\"",
                  (unsigned long)claw_tick_ms(), msg.text,
                  strlen(msg.text) > 80 ? "..." : "");
        send_reply(msg.chat_id, msg.text);
        CLAW_LOGI(TAG, "[%lu ms] >>> sent (heap=%u)",
                  (unsigned long)claw_tick_ms(),
                  (unsigned)esp_get_free_heap_size());
        claw_free(msg.text);
    }
}

static void enqueue_reply(const char *chat_id, const char *text)
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

    if (claw_mq_send(s_outbound_q, &msg, sizeof(msg), 1000)
            != CLAW_OK) {
        CLAW_LOGW(TAG, "outbound queue full, dropping reply");
        claw_free(copy);
    }
}

/* ------------------------------------------------------------------ */
/*  AI worker thread — inbound queue → ai_chat → outbound queue       */
/* ------------------------------------------------------------------ */

static void tg_ai_worker(void *arg)
{
    (void)arg;
    char *reply = claw_malloc(REPLY_BUF_SIZE);
    if (!reply) {
        CLAW_LOGE(TAG, "worker: no memory");
        return;
    }

    tg_inbound_t in;

    while (1) {
        if (claw_mq_recv(s_inbound_q, &in, sizeof(in),
                         CLAW_WAIT_FOREVER) != CLAW_OK) {
            continue;
        }

        /* Typing indicator before AI call */
        send_chat_action(in.chat_id);

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
                  (unsigned)esp_get_free_heap_size());
        int ret = ai_chat(in.text, reply, REPLY_BUF_SIZE);
        CLAW_LOGI(TAG, "[%lu ms] ai_chat ret=%d (heap=%u)",
                  (unsigned long)claw_tick_ms(), ret,
                  (unsigned)esp_get_free_heap_size());

        ai_set_channel_hint(NULL);
        sched_set_reply_context(NULL, NULL);

        if (ret == CLAW_OK && reply[0] != '\0') {
            enqueue_reply(in.chat_id, reply);
        } else {
            if (reply[0] != '\0') {
                enqueue_reply(in.chat_id, reply);
            } else {
                enqueue_reply(in.chat_id,
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
static void process_update(cJSON *update)
{
    cJSON *uid = cJSON_GetObjectItem(update, "update_id");
    if (!uid || !cJSON_IsNumber(uid)) {
        return;
    }

    /* Advance offset past this update */
    int64_t id = (int64_t)uid->valuedouble;
    if (id >= s_update_offset) {
        s_update_offset = id + 1;
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

    if (claw_mq_send(s_inbound_q, &in, sizeof(in), 0) != CLAW_OK) {
        CLAW_LOGW(TAG, "[%lu ms] !!! inbound queue full, dropping",
                  (unsigned long)claw_tick_ms());
    } else {
        CLAW_LOGI(TAG, "[%lu ms] --> queued",
                  (unsigned long)claw_tick_ms());
    }
}

static void tg_poll_thread(void *arg)
{
    (void)arg;

    CLAW_LOGI(TAG, "poll thread starting");

    char *resp = claw_malloc(RESP_BUF_SIZE);
    if (!resp) {
        CLAW_LOGE(TAG, "poll: no memory for response buffer");
        return;
    }

    for (;;) {
        char body[128];
        snprintf(body, sizeof(body),
                 "{\"offset\":%" PRId64 ",\"timeout\":%d}",
                 s_update_offset, POLL_TIMEOUT_SEC);

        resp[0] = '\0';
        int ret = tg_api_post("getUpdates", body, resp, RESP_BUF_SIZE,
                               HTTP_POLL_TIMEOUT_MS);
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
                process_update(cJSON_GetArrayItem(result, i));
            }
        }

        cJSON_Delete(root);
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void telegram_set_bot_token(const char *token)
{
    if (token) {
        snprintf(s_bot_token, sizeof(s_bot_token), "%s", token);
    }
}

const char *telegram_get_bot_token(void)
{
    return s_bot_token;
}

int telegram_init(void)
{
    if (s_bot_token[0] == '\0') {
        snprintf(s_bot_token, sizeof(s_bot_token),
                 "%s", CONFIG_RTCLAW_TELEGRAM_BOT_TOKEN);
    }

    if (s_bot_token[0] == '\0') {
        CLAW_LOGW(TAG, "no bot token configured, service disabled");
        return CLAW_OK;
    }

    /* Build API base URL: <api_url>/bot<token> */
    snprintf(s_api_base, sizeof(s_api_base),
             "%s/bot%s", CONFIG_RTCLAW_TELEGRAM_API_URL, s_bot_token);

    s_update_offset = 0;

    s_inbound_q = claw_mq_create("tg_in", sizeof(tg_inbound_t),
                                  INBOUND_DEPTH);
    s_outbound_q = claw_mq_create("tg_out", sizeof(tg_outbound_t),
                                   OUTBOUND_DEPTH);
    if (!s_inbound_q || !s_outbound_q) {
        CLAW_LOGE(TAG, "message queue create failed");
        return CLAW_ERROR;
    }

    CLAW_LOGI(TAG, "init ok");
    return CLAW_OK;
}

int telegram_start(void)
{
    if (s_bot_token[0] == '\0') {
        return CLAW_OK;
    }

    claw_thread_t w = claw_thread_create("tg_ai", tg_ai_worker,
                                          NULL, WORKER_STACK, 10);
    if (!w) {
        CLAW_LOGE(TAG, "failed to create AI worker");
        return CLAW_ERROR;
    }

    claw_thread_t o = claw_thread_create("tg_out", tg_outbound_thread,
                                          NULL, OUTBOUND_STACK, 10);
    if (!o) {
        CLAW_LOGE(TAG, "failed to create outbound thread");
        return CLAW_ERROR;
    }

    claw_thread_t p = claw_thread_create("tg_poll", tg_poll_thread,
                                          NULL, POLL_STACK, 10);
    if (!p) {
        CLAW_LOGE(TAG, "failed to create poll thread");
        return CLAW_ERROR;
    }

    CLAW_LOGI(TAG, "started (3 threads)");
    return CLAW_OK;
}

#else /* !CONFIG_RTCLAW_TELEGRAM_ENABLE */

int  telegram_init(void)  { return 0; }
int  telegram_start(void) { return 0; }
void telegram_set_bot_token(const char *t) { (void)t; }
const char *telegram_get_bot_token(void)   { return ""; }

#endif /* CONFIG_RTCLAW_TELEGRAM_ENABLE */

#else /* !CLAW_PLATFORM_ESP_IDF */

int  telegram_init(void)  { return 0; }
int  telegram_start(void) { return 0; }
void telegram_set_bot_token(const char *t) { (void)t; }
const char *telegram_get_bot_token(void)   { return ""; }

#endif /* CLAW_PLATFORM_ESP_IDF */
