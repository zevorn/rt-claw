/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Feishu (Lark) IM integration via long connection (WebSocket).
 *
 * Protocol:
 *   1. POST /callback/ws/endpoint with AppID+AppSecret to get WSS URL
 *   2. Connect WebSocket, receive Protobuf-framed events
 *   3. Forward user messages to ai_chat(), reply via HTTP API
 *
 * Feishu WebSocket frames use Protobuf encoding (Frame + Header).
 * We implement minimal hand-coded Protobuf encode/decode to avoid
 * pulling in a full protobuf library on an embedded target.
 */

#include "osal/claw_os.h"
#include "claw_config.h"
#include "claw/services/im/feishu.h"
#include "claw/services/im/im_util.h"
#include "claw/services/ai/ai_engine.h"
#include "claw/tools/claw_tools.h"

#include <string.h>
#include <stdio.h>

#define TAG "feishu"

#ifdef CLAW_PLATFORM_ESP_IDF

#include "sdkconfig.h"

#ifdef CONFIG_RTCLAW_FEISHU_ENABLE

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_websocket_client.h"
#include "esp_system.h"
#include "cJSON.h"

#define FEISHU_CRED_MAX     128

static char s_app_id[FEISHU_CRED_MAX];
static char s_app_secret[FEISHU_CRED_MAX];

#define WS_EP_URL    "https://open.feishu.cn/callback/ws/endpoint"
#define TOKEN_URL \
    "https://open.feishu.cn/open-apis/auth/v3/" \
    "tenant_access_token/internal"
#define MSG_SEND_URL "https://open.feishu.cn/open-apis/im/v1/messages"

#define TOKEN_BUF_SIZE      256
#define RESP_BUF_SIZE       2048
#define REPLY_BUF_SIZE      4096
#define PING_INTERVAL_MS    (120 * 1000)
#define WS_RECONNECT_MS     5000
#define TOKEN_REFRESH_MS    (90 * 60 * 1000)
#define HTTP_TIMEOUT_MS     15000
#define TOKEN_MAX_RETRIES   30

/* ------------------------------------------------------------------ */
/*  State                                                              */
/* ------------------------------------------------------------------ */

static char s_token[TOKEN_BUF_SIZE];
static claw_mutex_t s_lock;
static esp_websocket_client_handle_t s_ws_client;
/* Accessed from both WS callback and worker thread */
static volatile int s_ws_connected;

/* Event dedup ring buffer — drop events already processed */
#define DEDUP_SLOTS  8
#define EVENT_ID_MAX 48
static char s_dedup[DEDUP_SLOTS][EVENT_ID_MAX];
static int  s_dedup_idx;

/*
 * Stale event filter based on create_time (unix ms from event header).
 * Track the latest create_time seen; drop events older than threshold.
 */
static uint64_t s_latest_event_ts;
#define STALE_EVENT_MAX_AGE_MS  60000

/* Parse decimal string to uint64 (avoids checkpatch strtoull warning) */
static uint64_t parse_u64(const char *s)
{
    uint64_t v = 0;

    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v;
}

static int dedup_check_and_add(const char *event_id)
{
    for (int i = 0; i < DEDUP_SLOTS; i++) {
        if (strcmp(s_dedup[i], event_id) == 0) {
            return 1; /* duplicate */
        }
    }
    snprintf(s_dedup[s_dedup_idx], EVENT_ID_MAX, "%s", event_id);
    s_dedup_idx = (s_dedup_idx + 1) % DEDUP_SLOTS;
    return 0;
}

struct http_ctx {
    char   *buf;
    size_t  len;
    size_t  cap;
};

/* ------------------------------------------------------------------ */
/*  Minimal Protobuf helpers for Feishu Frame                          */
/*                                                                     */
/*  Frame {                                                            */
/*    1: uint64 SeqID                                                  */
/*    2: uint64 LogID                                                  */
/*    3: int32  service                                                */
/*    4: int32  method    (0=CONTROL, 1=DATA)                          */
/*    5: Header headers[] (key+value strings)                          */
/*    6: string payload_encoding                                       */
/*    7: string payload_type                                           */
/*    8: bytes  payload                                                */
/*    9: string LogIDNew                                               */
/*  }                                                                  */
/* ------------------------------------------------------------------ */

/* Protobuf wire types */
#define PB_VARINT   0
#define PB_LEN      2

/* Decode a varint, return bytes consumed (0 on error) */
static int pb_decode_varint(const uint8_t *buf, int len, uint64_t *val)
{
    *val = 0;
    int shift = 0;

    for (int i = 0; i < len && i < 10; i++) {
        *val |= (uint64_t)(buf[i] & 0x7F) << shift;
        shift += 7;
        if ((buf[i] & 0x80) == 0) {
            return i + 1;
        }
    }
    return 0;
}

/* Encode a varint, return bytes written */
static int pb_encode_varint(uint8_t *buf, uint64_t val)
{
    int n = 0;

    do {
        buf[n] = (uint8_t)(val & 0x7F);
        val >>= 7;
        if (val) {
            buf[n] |= 0x80;
        }
        n++;
    } while (val);
    return n;
}

/* Encode field tag */
static int pb_encode_tag(uint8_t *buf, int field, int wire_type)
{
    return pb_encode_varint(buf, (uint64_t)((field << 3) | wire_type));
}

/*
 * Parse a Feishu Frame from raw protobuf bytes.
 * We only extract: method (field 4), headers (field 5), payload (field 8).
 */
struct feishu_frame {
    int          method;
    const char  *type;          /* from header "type" */
    int          type_len;
    const char  *msg_id;        /* from header "message_id" */
    int          msg_id_len;
    const uint8_t *payload;
    int          payload_len;
};

static int parse_frame(const uint8_t *buf, int len, struct feishu_frame *f)
{
    memset(f, 0, sizeof(*f));
    f->method = -1;
    int pos = 0;

    while (pos < len) {
        uint64_t tag;
        int n = pb_decode_varint(buf + pos, len - pos, &tag);
        if (n == 0) {
            break;
        }
        pos += n;

        int field = (int)(tag >> 3);
        int wire = (int)(tag & 0x07);

        if (wire == PB_VARINT) {
            uint64_t val;
            n = pb_decode_varint(buf + pos, len - pos, &val);
            if (n == 0) {
                break;
            }
            pos += n;
            if (field == 4) {
                f->method = (int)val;
            }
        } else if (wire == PB_LEN) {
            uint64_t slen;
            n = pb_decode_varint(buf + pos, len - pos, &slen);
            if (n == 0) {
                break;
            }
            pos += n;
            if (pos + (int)slen > len) {
                break;
            }

            if (field == 8) {
                /* payload */
                f->payload = buf + pos;
                f->payload_len = (int)slen;
            } else if (field == 5) {
                /* Embedded Header message: parse key (1) + value (2) */
                const uint8_t *hbuf = buf + pos;
                int hlen = (int)slen;
                const char *key = NULL;
                int key_len = 0;
                const char *val_str = NULL;
                int val_len = 0;
                int hp = 0;

                while (hp < hlen) {
                    uint64_t htag;
                    int hn = pb_decode_varint(hbuf + hp, hlen - hp, &htag);
                    if (hn == 0) {
                        break;
                    }
                    hp += hn;
                    int hfield = (int)(htag >> 3);
                    int hwire = (int)(htag & 0x07);

                    if (hwire == PB_LEN) {
                        uint64_t sl;
                        hn = pb_decode_varint(hbuf + hp, hlen - hp, &sl);
                        if (hn == 0) {
                            break;
                        }
                        hp += hn;
                        if (hfield == 1) {
                            key = (const char *)(hbuf + hp);
                            key_len = (int)sl;
                        } else if (hfield == 2) {
                            val_str = (const char *)(hbuf + hp);
                            val_len = (int)sl;
                        }
                        hp += (int)sl;
                    } else {
                        break;
                    }
                }

                if (key && val_str) {
                    if (key_len == 4 && memcmp(key, "type", 4) == 0) {
                        f->type = val_str;
                        f->type_len = val_len;
                    } else if (key_len == 10 &&
                               memcmp(key, "message_id", 10) == 0) {
                        f->msg_id = val_str;
                        f->msg_id_len = val_len;
                    }
                }
            }
            pos += (int)slen;
        } else {
            /* Unknown wire type — skip is unsafe, abort */
            break;
        }
    }
    return (f->method >= 0) ? 0 : -1;
}

/*
 * Encode a single Header sub-message { key(1)=k, value(2)=v }
 * into buf, return bytes written.
 */
static int pb_encode_header(uint8_t *buf, int cap,
                            const char *k, int klen,
                            const char *v, int vlen)
{
    uint8_t inner[128];
    int ip = 0;

    if (klen + vlen + 10 > (int)sizeof(inner)) {
        return 0;
    }

    ip += pb_encode_tag(inner + ip, 1, PB_LEN);
    ip += pb_encode_varint(inner + ip, klen);
    memcpy(inner + ip, k, klen);
    ip += klen;
    ip += pb_encode_tag(inner + ip, 2, PB_LEN);
    ip += pb_encode_varint(inner + ip, vlen);
    memcpy(inner + ip, v, vlen);
    ip += vlen;

    /* field 5 (Header) length-delimited */
    int pos = 0;
    pos += pb_encode_tag(buf + pos, 5, PB_LEN);
    pos += pb_encode_varint(buf + pos, ip);
    if (pos + ip > cap) {
        return 0;
    }
    memcpy(buf + pos, inner, ip);
    pos += ip;
    return pos;
}

/*
 * Build a minimal Protobuf ping frame (method=0, header type="pong").
 * Returns encoded length.
 */
static int build_pong_frame(uint8_t *buf, int cap)
{
    int pos = 0;

    /* field 4: method = 0 (CONTROL) */
    pos += pb_encode_tag(buf + pos, 4, PB_VARINT);
    pos += pb_encode_varint(buf + pos, 0);

    pos += pb_encode_header(buf + pos, cap - pos, "type", 4, "pong", 4);

    return pos;
}

/*
 * Build an ACK frame for a received DATA event.
 * Feishu requires the client to echo back the message_id to acknowledge
 * receipt; otherwise the event will be re-delivered.
 */
/*
 * ACK payload — Feishu requires `{"code":0}` in the response payload
 * to confirm successful processing. Without it the event is re-delivered.
 */
static const char ACK_PAYLOAD[] = "{\"code\":0}";

static int build_ack_frame(uint8_t *buf, int cap,
                           const char *msg_id, int msg_id_len)
{
    int pos = 0;
    int n;

    /* field 3: service = 0 */
    pos += pb_encode_tag(buf + pos, 3, PB_VARINT);
    pos += pb_encode_varint(buf + pos, 0);

    /* field 4: method = 0 (CONTROL) */
    pos += pb_encode_tag(buf + pos, 4, PB_VARINT);
    pos += pb_encode_varint(buf + pos, 0);

    /* Header: type = "event_ack" */
    n = pb_encode_header(buf + pos, cap - pos,
                         "type", 4, "event_ack", 9);
    if (n == 0) {
        return 0;
    }
    pos += n;

    /* Header: message_id = <msg_id> */
    if (msg_id && msg_id_len > 0) {
        n = pb_encode_header(buf + pos, cap - pos,
                             "message_id", 10, msg_id, msg_id_len);
        if (n == 0) {
            return 0;
        }
        pos += n;
    }

    /* field 8: payload = {"code":0} */
    int plen = (int)sizeof(ACK_PAYLOAD) - 1;
    pos += pb_encode_tag(buf + pos, 8, PB_LEN);
    pos += pb_encode_varint(buf + pos, plen);
    if (pos + plen > cap) {
        return 0;
    }
    memcpy(buf + pos, ACK_PAYLOAD, plen);
    pos += plen;

    return pos;
}

/* ------------------------------------------------------------------ */
/*  HTTP helpers                                                       */
/* ------------------------------------------------------------------ */

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

static int http_post_json(const char *url, const char *auth_header,
                          const char *body, char *resp, size_t resp_size)
{
    struct http_ctx ctx = { .buf = resp, .len = 0, .cap = resp_size };

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
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
    if (auth_header) {
        esp_http_client_set_header(client, "Authorization", auth_header);
    }
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status < 200 || status >= 300) {
        CLAW_LOGE(TAG, "HTTP POST %s failed: err=%d status=%d",
                  url, err, status);
        return CLAW_ERROR;
    }
    return CLAW_OK;
}

/* ------------------------------------------------------------------ */
/*  Token management (for sending replies via REST API)                */
/* ------------------------------------------------------------------ */

static int refresh_token(void)
{
    char *resp = claw_malloc(RESP_BUF_SIZE);
    if (!resp) {
        return CLAW_NOMEM;
    }

    char body[256];
    snprintf(body, sizeof(body),
             "{\"app_id\":\"%s\",\"app_secret\":\"%s\"}",
             s_app_id, s_app_secret);

    int ret = http_post_json(TOKEN_URL, NULL, body, resp, RESP_BUF_SIZE);
    if (ret != CLAW_OK) {
        claw_free(resp);
        return ret;
    }

    cJSON *root = cJSON_Parse(resp);
    claw_free(resp);
    if (!root) {
        return CLAW_ERROR;
    }

    cJSON *token = cJSON_GetObjectItem(root, "tenant_access_token");
    if (!token || !cJSON_IsString(token)) {
        cJSON_Delete(root);
        CLAW_LOGE(TAG, "token response missing tenant_access_token");
        return CLAW_ERROR;
    }

    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);
    snprintf(s_token, sizeof(s_token), "%s", token->valuestring);
    claw_mutex_unlock(s_lock);

    CLAW_LOGI(TAG, "tenant token refreshed");
    cJSON_Delete(root);
    return CLAW_OK;
}

static void get_auth_header(char *out, size_t size)
{
    claw_mutex_lock(s_lock, CLAW_WAIT_FOREVER);
    snprintf(out, size, "Bearer %s", s_token);
    claw_mutex_unlock(s_lock);
}

/* ------------------------------------------------------------------ */
/*  Send reply to Feishu via REST API                                  */
/* ------------------------------------------------------------------ */

#define FEISHU_MAX_MSG_LEN  4000

static int send_one_card(const char *chat_id, const char *auth,
                         const char *text)
{
    cJSON *card = cJSON_CreateObject();
    cJSON *elements = cJSON_AddArrayToObject(card, "elements");
    cJSON *md_elem = cJSON_CreateObject();
    cJSON_AddStringToObject(md_elem, "tag", "markdown");
    cJSON_AddStringToObject(md_elem, "content", text);
    cJSON_AddItemToArray(elements, md_elem);

    char *content_str = cJSON_PrintUnformatted(card);
    cJSON_Delete(card);
    if (!content_str) {
        return CLAW_NOMEM;
    }

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "receive_id", chat_id);
    cJSON_AddStringToObject(body, "msg_type", "interactive");
    cJSON_AddStringToObject(body, "content", content_str);
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    claw_free(content_str);
    if (!body_str) {
        return CLAW_NOMEM;
    }

    char url[128];
    snprintf(url, sizeof(url), "%s?receive_id_type=chat_id",
             MSG_SEND_URL);

    char resp[1024];
    int ret = http_post_json(url, auth, body_str, resp, sizeof(resp));
    claw_free(body_str);

    if (ret != CLAW_OK) {
        CLAW_LOGE(TAG, "send reply failed: %.200s", resp);
    }
    return ret;
}

/*
 * Send reply with auto-chunking.  Messages longer than
 * FEISHU_MAX_MSG_LEN are split at the last newline before
 * the limit to avoid breaking mid-sentence.
 */
static int send_reply(const char *chat_id, const char *text)
{
    char auth[TOKEN_BUF_SIZE + 16];
    get_auth_header(auth, sizeof(auth));

    size_t total = strlen(text);
    if (total <= FEISHU_MAX_MSG_LEN) {
        return send_one_card(chat_id, auth, text);
    }

    const char *p = text;
    size_t remaining = total;
    int part = 1;

    while (remaining > 0) {
        size_t chunk = im_find_chunk_end(p, remaining,
                                         FEISHU_MAX_MSG_LEN);

        char *chunk_buf = claw_malloc(chunk + 1);
        if (!chunk_buf) {
            return CLAW_NOMEM;
        }
        memcpy(chunk_buf, p, chunk);
        chunk_buf[chunk] = '\0';

        CLAW_LOGI(TAG, "send part %d (%d bytes)", part, (int)chunk);
        int ret = send_one_card(chat_id, auth, chunk_buf);
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

/* ------------------------------------------------------------------ */
/*  Reaction — add an emoji reaction to a user message                 */
/* ------------------------------------------------------------------ */

static void add_reaction(const char *message_id, const char *emoji_type)
{
    char auth[TOKEN_BUF_SIZE + 16];
    get_auth_header(auth, sizeof(auth));

    char url[256];
    snprintf(url, sizeof(url),
             "https://open.feishu.cn/open-apis/im/v1/messages/%s/reactions",
             message_id);

    cJSON *body = cJSON_CreateObject();
    cJSON *rt = cJSON_AddObjectToObject(body, "reaction_type");
    cJSON_AddStringToObject(rt, "emoji_type", emoji_type);
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!body_str) {
        return;
    }

    char resp[256];
    int ret = http_post_json(url, auth, body_str, resp, sizeof(resp));
    claw_free(body_str);
    if (ret != CLAW_OK) {
        CLAW_LOGW(TAG, "add reaction failed for msg %s", message_id);
    }
}

/* Forward declaration — defined after outbound_thread */
static void enqueue_reply(const char *chat_id, const char *msg_id,
                          const char *text);

/* ------------------------------------------------------------------ */
/*  Scheduled-task reply callback — routes results back to Feishu      */
/* ------------------------------------------------------------------ */

static void sched_reply_to_feishu(const char *target, const char *text)
{
    if (target && target[0] != '\0' && text && text[0] != '\0') {
        enqueue_reply(target, NULL, text);
    }
}

/* ------------------------------------------------------------------ */
/*  Message bus — inbound queue (WS→AI) + outbound queue (AI→HTTP)     */
/* ------------------------------------------------------------------ */

#define MSG_TEXT_MAX   1024
#define CHAT_ID_MAX   128
#define MSG_ID_MAX    128
#define WORKER_STACK  16384
#define OUTBOUND_STACK 8192

#define INBOUND_DEPTH  4
#define OUTBOUND_DEPTH 4

/* Inbound: from Feishu WebSocket to AI worker */
typedef struct {
    char text[MSG_TEXT_MAX];
    char chat_id[CHAT_ID_MAX];
    char msg_id[MSG_ID_MAX];
} feishu_inbound_t;

/* Outbound: from AI worker to HTTP sender */
typedef struct {
    char chat_id[CHAT_ID_MAX];
    char msg_id[MSG_ID_MAX]; /* original msg — for typing reaction */
    char *text;              /* heap-allocated, consumer frees */
} feishu_outbound_t;

static claw_mq_t s_inbound_q;
static claw_mq_t s_outbound_q;

/* ---- Outbound dispatch thread ---- */

static void outbound_thread(void *arg)
{
    (void)arg;
    feishu_outbound_t msg;

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

static void enqueue_reply(const char *chat_id, const char *msg_id,
                          const char *text)
{
    size_t len = strlen(text);
    char *copy = claw_malloc(len + 1);
    if (!copy) {
        CLAW_LOGE(TAG, "outbound: no memory for reply");
        return;
    }
    memcpy(copy, text, len + 1);

    feishu_outbound_t msg;
    snprintf(msg.chat_id, sizeof(msg.chat_id), "%s", chat_id);
    snprintf(msg.msg_id, sizeof(msg.msg_id), "%s",
             msg_id ? msg_id : "");
    msg.text = copy;

    if (claw_mq_send(s_outbound_q, &msg, sizeof(msg), 1000)
            != CLAW_OK) {
        CLAW_LOGW(TAG, "outbound queue full, dropping reply");
        claw_free(copy);
    }
}

/* ---- AI worker thread ---- */

static void ai_worker_thread(void *arg)
{
    (void)arg;
    char *reply = claw_malloc(REPLY_BUF_SIZE);
    if (!reply) {
        CLAW_LOGE(TAG, "worker: no memory");
        return;
    }

    feishu_inbound_t in;

    while (1) {
        if (claw_mq_recv(s_inbound_q, &in, sizeof(in),
                         CLAW_WAIT_FOREVER) != CLAW_OK) {
            continue;
        }

        /* Immediate typing indicator — before AI call */
        if (in.msg_id[0] != '\0') {
            add_reaction(in.msg_id, "Typing");
        }

        ai_set_channel(AI_CHANNEL_FEISHU);
        ai_set_channel_hint(
            " You are communicating via Feishu IM."
            " All outputs (including scheduled task results)"
            " will be delivered to this Feishu conversation."
            " Do NOT mention LCD or serial console unless"
            " the user explicitly asks."
            " IMPORTANT: Feishu does NOT render markdown tables."
            " Never use table syntax (| --- |). Use bullet lists"
            " or structured text instead.");
        sched_set_reply_context(sched_reply_to_feishu, in.chat_id);

        CLAW_LOGI(TAG, "[%lu ms] ai_chat: \"%s\" (heap=%u)",
                  (unsigned long)claw_tick_ms(), in.text,
                  (unsigned)esp_get_free_heap_size());
        int ret = ai_chat(in.text, reply, REPLY_BUF_SIZE);
        CLAW_LOGI(TAG, "[%lu ms] ai_chat ret=%d (heap=%u)",
                  (unsigned long)claw_tick_ms(), ret,
                  (unsigned)esp_get_free_heap_size());

        ai_set_channel(AI_CHANNEL_SHELL);
        ai_set_channel_hint(NULL);
        sched_set_reply_context(NULL, NULL);

        if (ret == CLAW_OK && reply[0] != '\0') {
            enqueue_reply(in.chat_id, in.msg_id, reply);
        } else {
            /*
             * Forward the actual error message from ai_chat()
             * so the user sees what went wrong (e.g. "API request
             * failed", "no API key", "AI is busy").
             */
            if (reply[0] != '\0') {
                enqueue_reply(in.chat_id, in.msg_id, reply);
            } else {
                enqueue_reply(in.chat_id, in.msg_id,
                              "[rt-claw] AI engine error");
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Process incoming event payload (JSON inside protobuf frame)        */
/* ------------------------------------------------------------------ */

static void handle_message_event(cJSON *event)
{
    cJSON *message = cJSON_GetObjectItem(event, "message");
    if (!message) {
        return;
    }

    cJSON *msg_type = cJSON_GetObjectItem(message, "message_type");
    if (!msg_type || !cJSON_IsString(msg_type) ||
        strcmp(msg_type->valuestring, "text") != 0) {
        CLAW_LOGD(TAG, "ignore non-text message");
        return;
    }

    cJSON *content_raw = cJSON_GetObjectItem(message, "content");
    if (!content_raw || !cJSON_IsString(content_raw)) {
        return;
    }

    cJSON *content = cJSON_Parse(content_raw->valuestring);
    if (!content) {
        return;
    }

    cJSON *text = cJSON_GetObjectItem(content, "text");
    if (!text || !cJSON_IsString(text)) {
        cJSON_Delete(content);
        return;
    }

    const char *user_msg = text->valuestring;

    cJSON *msg_id_j = cJSON_GetObjectItem(message, "message_id");
    const char *mid = (msg_id_j && cJSON_IsString(msg_id_j))
                      ? msg_id_j->valuestring : "?";

    CLAW_LOGI(TAG, "[%lu ms] <<< recv msg_id=%s text=\"%s\"",
              (unsigned long)claw_tick_ms(), mid, user_msg);

    cJSON *chat_id = cJSON_GetObjectItem(message, "chat_id");
    if (!chat_id || !cJSON_IsString(chat_id)) {
        cJSON_Delete(content);
        return;
    }

    /* Push to inbound queue — non-blocking, drop if full */
    feishu_inbound_t in;
    snprintf(in.text, sizeof(in.text), "%s", user_msg);
    snprintf(in.chat_id, sizeof(in.chat_id), "%s",
             chat_id->valuestring);
    snprintf(in.msg_id, sizeof(in.msg_id), "%s", mid);

    if (claw_mq_send(s_inbound_q, &in, sizeof(in), 0) != CLAW_OK) {
        CLAW_LOGW(TAG, "[%lu ms] !!! inbound queue full, "
                  "dropping msg_id=%s",
                  (unsigned long)claw_tick_ms(), mid);
    } else {
        CLAW_LOGI(TAG, "[%lu ms] --> queued (depth=%d)",
                  (unsigned long)claw_tick_ms(), INBOUND_DEPTH);
    }

    cJSON_Delete(content);
}

static void process_event_payload(const uint8_t *data, int len)
{
    cJSON *root = cJSON_ParseWithLength((const char *)data, len);
    if (!root) {
        CLAW_LOGW(TAG, "event payload JSON parse failed");
        return;
    }

    CLAW_LOGI(TAG, "event: %.200s", (const char *)data);

    cJSON *header = cJSON_GetObjectItem(root, "header");
    if (header) {
        /* Dedup by event_id */
        cJSON *eid = cJSON_GetObjectItem(header, "event_id");
        if (eid && cJSON_IsString(eid)) {
            if (dedup_check_and_add(eid->valuestring)) {
                CLAW_LOGW(TAG, "[%lu ms] dup event_id=%s, dropped",
                          (unsigned long)claw_tick_ms(),
                          eid->valuestring);
                cJSON_Delete(root);
                return;
            }
        }

        /* Drop stale events based on create_time (unix ms) */
        cJSON *ct = cJSON_GetObjectItem(header, "create_time");
        if (ct && cJSON_IsString(ct)) {
            uint64_t ts = parse_u64(ct->valuestring);
            if (s_latest_event_ts > 0 &&
                ts + STALE_EVENT_MAX_AGE_MS < s_latest_event_ts) {
                CLAW_LOGW(TAG, "[%lu ms] stale event "
                          "(create_time=%llu, latest=%llu), dropped",
                          (unsigned long)claw_tick_ms(),
                          (unsigned long long)ts,
                          (unsigned long long)s_latest_event_ts);
                cJSON_Delete(root);
                return;
            }
            if (ts > s_latest_event_ts) {
                s_latest_event_ts = ts;
            }
        }

        cJSON *event_type = cJSON_GetObjectItem(header, "event_type");
        if (event_type && cJSON_IsString(event_type)) {
            if (strcmp(event_type->valuestring,
                       "im.message.receive_v1") == 0) {
                cJSON *event = cJSON_GetObjectItem(root, "event");
                if (event) {
                    handle_message_event(event);
                }
            } else {
                CLAW_LOGI(TAG, "unhandled event: %s",
                          event_type->valuestring);
            }
        }
    }

    cJSON_Delete(root);
}

/* ------------------------------------------------------------------ */
/*  WebSocket event handler                                            */
/* ------------------------------------------------------------------ */

static void ws_event_handler(void *arg, esp_event_base_t base,
                             int32_t event_id, void *event_data)
{
    (void)arg;
    (void)base;
    esp_websocket_event_data_t *ws = event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        CLAW_LOGI(TAG, "ws connected");
        s_ws_connected = 1;
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        CLAW_LOGW(TAG, "ws disconnected");
        s_ws_connected = 0;
        break;

    case WEBSOCKET_EVENT_DATA:
        if (ws->op_code == 0x02 && ws->data_len > 0) {
            /* Binary frame — Protobuf encoded */
            struct feishu_frame f;
            if (parse_frame((const uint8_t *)ws->data_ptr,
                            ws->data_len, &f) == 0) {
                if (f.type && f.type_len == 4 &&
                    memcmp(f.type, "ping", 4) == 0) {
                    /* Respond with pong */
                    uint8_t pong[64];
                    int plen = build_pong_frame(pong, sizeof(pong));
                    if (plen > 0 && s_ws_client) {
                        esp_websocket_client_send_bin(
                            s_ws_client, (const char *)pong, plen,
                            portMAX_DELAY);
                    }
                    CLAW_LOGD(TAG, "ping/pong");
                } else if (f.method == 1 && f.payload && f.payload_len > 0) {
                    /* DATA frame — payload is JSON event */
                    CLAW_LOGI(TAG, "[%lu ms] ws DATA frame, "
                              "msg_id_len=%d, payload_len=%d",
                              (unsigned long)claw_tick_ms(),
                              f.msg_id_len, f.payload_len);

                    /* ACK immediately so Feishu won't re-deliver */
                    if (f.msg_id && f.msg_id_len > 0) {
                        uint8_t ack[128];
                        int alen = build_ack_frame(ack, sizeof(ack),
                                                   f.msg_id,
                                                   f.msg_id_len);
                        if (alen > 0 && s_ws_client) {
                            esp_websocket_client_send_bin(
                                s_ws_client, (const char *)ack,
                                alen, portMAX_DELAY);
                            CLAW_LOGI(TAG, "[%lu ms] sent ACK",
                                      (unsigned long)claw_tick_ms());
                        }
                    }

                    process_event_payload(f.payload, f.payload_len);
                }
            }
        } else if (ws->op_code == 0x01 && ws->data_len > 0) {
            /* Text frame fallback — try as JSON directly */
            process_event_payload((const uint8_t *)ws->data_ptr,
                                  ws->data_len);
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        CLAW_LOGE(TAG, "ws error");
        s_ws_connected = 0;
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/*  Fetch WebSocket endpoint and connect                               */
/* ------------------------------------------------------------------ */

static int connect_ws(void)
{
    char *resp = claw_malloc(RESP_BUF_SIZE);
    if (!resp) {
        return CLAW_NOMEM;
    }

    /* Authenticate directly with AppID + AppSecret in body */
    char body[256];
    snprintf(body, sizeof(body),
             "{\"AppID\":\"%s\",\"AppSecret\":\"%s\"}",
             s_app_id, s_app_secret);

    int ret = http_post_json(WS_EP_URL, NULL, body, resp, RESP_BUF_SIZE);
    if (ret != CLAW_OK) {
        CLAW_LOGE(TAG, "ws endpoint request failed");
        claw_free(resp);
        return ret;
    }

    CLAW_LOGI(TAG, "ws endpoint resp: %.200s", resp);

    cJSON *root = cJSON_Parse(resp);
    claw_free(resp);
    if (!root) {
        CLAW_LOGE(TAG, "ws endpoint JSON parse failed");
        return CLAW_ERROR;
    }

    /* Check error code */
    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (code && cJSON_IsNumber(code) && code->valueint != 0) {
        cJSON *msg = cJSON_GetObjectItem(root, "msg");
        CLAW_LOGE(TAG, "ws endpoint error: code=%d msg=%s",
                  code->valueint,
                  (msg && cJSON_IsString(msg)) ? msg->valuestring : "?");
        cJSON_Delete(root);
        return CLAW_ERROR;
    }

    /* Extract WSS URL: { "data": { "URL": "wss://..." } } */
    cJSON *data_obj = cJSON_GetObjectItem(root, "data");
    cJSON *url_obj = data_obj ? cJSON_GetObjectItem(data_obj, "URL") : NULL;
    if (!url_obj || !cJSON_IsString(url_obj)) {
        CLAW_LOGE(TAG, "ws endpoint missing URL field");
        cJSON_Delete(root);
        return CLAW_ERROR;
    }

    CLAW_LOGI(TAG, "ws url: %s", url_obj->valuestring);

    esp_websocket_client_config_t ws_cfg = {
        .uri = url_obj->valuestring,
        .buffer_size = 4096,
        .task_stack = 8192,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .reconnect_timeout_ms = WS_RECONNECT_MS,
        .network_timeout_ms = 10000,
    };

    s_ws_client = esp_websocket_client_init(&ws_cfg);
    cJSON_Delete(root);
    if (!s_ws_client) {
        return CLAW_ERROR;
    }

    esp_websocket_register_events(s_ws_client, WEBSOCKET_EVENT_ANY,
                                  ws_event_handler, NULL);
    esp_err_t err = esp_websocket_client_start(s_ws_client);
    if (err != ESP_OK) {
        CLAW_LOGE(TAG, "ws start failed: %d", err);
        esp_websocket_client_destroy(s_ws_client);
        s_ws_client = NULL;
        return CLAW_ERROR;
    }

    return CLAW_OK;
}

/* ------------------------------------------------------------------ */
/*  Main service thread                                                */
/* ------------------------------------------------------------------ */

static void feishu_thread(void *arg)
{
    (void)arg;

    CLAW_LOGI(TAG, "service starting (app_id=%s)", s_app_id);

    /* Fetch tenant token first (needed for sending replies) */
    int retries = 0;

    while (refresh_token() != CLAW_OK) {
        retries++;
        if (retries >= TOKEN_MAX_RETRIES) {
            CLAW_LOGE(TAG, "token fetch failed after %d retries, "
                      "giving up", retries);
            return;
        }
        CLAW_LOGW(TAG, "token fetch failed, retry in 10s");
        claw_thread_delay_ms(10000);
    }
    CLAW_LOGI(TAG, "token acquired");

    /* Connect WebSocket long connection */
    while (connect_ws() != CLAW_OK) {
        CLAW_LOGW(TAG, "ws connect failed, retry in %dms",
                  WS_RECONNECT_MS);
        claw_thread_delay_ms(WS_RECONNECT_MS);
    }
    CLAW_LOGI(TAG, "ws connected — ready to receive messages");

    /* Keep-alive loop */
    uint32_t last_refresh = claw_tick_ms();

    for (;;) {
        claw_thread_delay_ms(5000);

        /* Periodic token refresh */
        if (claw_tick_ms() - last_refresh >= TOKEN_REFRESH_MS) {
            refresh_token();
            last_refresh = claw_tick_ms();
        }

        /* Reconnect on disconnect */
        if (!s_ws_connected && s_ws_client) {
            CLAW_LOGW(TAG, "ws lost, reconnecting...");
            esp_websocket_client_destroy(s_ws_client);
            s_ws_client = NULL;
            claw_thread_delay_ms(WS_RECONNECT_MS);
            connect_ws();
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void feishu_set_app_id(const char *app_id)
{
    if (app_id) {
        snprintf(s_app_id, sizeof(s_app_id), "%s", app_id);
    }
}

void feishu_set_app_secret(const char *app_secret)
{
    if (app_secret) {
        snprintf(s_app_secret, sizeof(s_app_secret), "%s", app_secret);
    }
}

const char *feishu_get_app_id(void)     { return s_app_id; }
const char *feishu_get_app_secret(void) { return s_app_secret; }

int feishu_init(void)
{
    /* Initialize from compile-time defaults if not set via setter */
    if (s_app_id[0] == '\0') {
        snprintf(s_app_id, sizeof(s_app_id),
                 "%s", CONFIG_RTCLAW_FEISHU_APP_ID);
    }
    if (s_app_secret[0] == '\0') {
        snprintf(s_app_secret, sizeof(s_app_secret),
                 "%s", CONFIG_RTCLAW_FEISHU_APP_SECRET);
    }

    s_lock = claw_mutex_create("feishu");
    if (!s_lock) {
        return CLAW_ERROR;
    }

    s_inbound_q = claw_mq_create("fs_in",
                                  sizeof(feishu_inbound_t),
                                  INBOUND_DEPTH);
    s_outbound_q = claw_mq_create("fs_out",
                                   sizeof(feishu_outbound_t),
                                   OUTBOUND_DEPTH);
    if (!s_inbound_q || !s_outbound_q) {
        CLAW_LOGE(TAG, "message queue create failed");
        return CLAW_ERROR;
    }

    s_token[0] = '\0';
    s_ws_client = NULL;
    s_ws_connected = 0;
    CLAW_LOGI(TAG, "init ok");
    return CLAW_OK;
}

int feishu_start(void)
{
    /* AI worker: inbound queue → ai_chat → outbound queue */
    claw_thread_t w = claw_thread_create("fs_ai", ai_worker_thread,
                                          NULL, WORKER_STACK, 10);
    if (!w) {
        CLAW_LOGE(TAG, "failed to create AI worker");
        return CLAW_ERROR;
    }

    /* Outbound dispatch: outbound queue → send_reply (HTTP) */
    claw_thread_t o = claw_thread_create("fs_out", outbound_thread,
                                          NULL, OUTBOUND_STACK, 10);
    if (!o) {
        CLAW_LOGE(TAG, "failed to create outbound thread");
        return CLAW_ERROR;
    }

    /* Feishu WebSocket connection thread */
    claw_thread_t t = claw_thread_create("feishu", feishu_thread, NULL,
                                         8192, 10);
    if (!t) {
        CLAW_LOGE(TAG, "failed to create thread");
        return CLAW_ERROR;
    }
    return CLAW_OK;
}

#else /* !CONFIG_RTCLAW_FEISHU_ENABLE */

int  feishu_init(void)  { return 0; }
int  feishu_start(void) { return 0; }
void feishu_set_app_id(const char *id)     { (void)id; }
void feishu_set_app_secret(const char *s)  { (void)s; }
const char *feishu_get_app_id(void)        { return ""; }
const char *feishu_get_app_secret(void)    { return ""; }

#endif /* CONFIG_RTCLAW_FEISHU_ENABLE */

#else /* !CLAW_PLATFORM_ESP_IDF */

int  feishu_init(void)  { return 0; }
int  feishu_start(void) { return 0; }
void feishu_set_app_id(const char *id)     { (void)id; }
void feishu_set_app_secret(const char *s)  { (void)s; }
const char *feishu_get_app_id(void)        { return ""; }
const char *feishu_get_app_secret(void)    { return ""; }

#endif /* CLAW_PLATFORM_ESP_IDF */
