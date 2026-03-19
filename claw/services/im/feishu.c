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
 *
 * OOP: private context struct embedding struct claw_service.
 */

#include "osal/claw_os.h"
#include "claw_config.h"
#include "claw/services/im/feishu.h"
#include "claw/services/im/im_util.h"
#include "claw/services/ai/ai_engine.h"
#include "claw/services/ai/ai_skill.h"
#include "claw/shell/shell_cmd.h"
#include "claw/tools/claw_tools.h"
#include "claw/core/claw_service.h"
#include "utils/list.h"

#include <string.h>
#include <stdio.h>

#define TAG "feishu"

#ifdef CONFIG_RTCLAW_FEISHU_ENABLE

#include "osal/claw_net.h"
#include "cJSON.h"

#ifdef CLAW_PLATFORM_ESP_IDF
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"
#elif defined(CLAW_PLATFORM_LINUX)
#include <curl/curl.h>
#include <curl/websockets.h>
#endif

#define FEISHU_CRED_MAX     128

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

#define DEDUP_SLOTS  8
#define EVENT_ID_MAX 48
#define STALE_EVENT_MAX_AGE_MS  60000

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

/* ------------------------------------------------------------------ */
/*  Service context — all state lives here, no file-scope globals      */
/* ------------------------------------------------------------------ */

struct feishu_ctx {
    struct claw_service  base;

    /* Credentials */
    char                 app_id[FEISHU_CRED_MAX];
    char                 app_secret[FEISHU_CRED_MAX];

    /* Tenant token (for REST API replies) */
    char                 token[TOKEN_BUF_SIZE];
    struct claw_mutex   *lock;

    /* WebSocket handle */
#ifdef CLAW_PLATFORM_ESP_IDF
    esp_websocket_client_handle_t ws_client;
#elif defined(CLAW_PLATFORM_LINUX)
    CURL                *ws_curl;
#endif
    int                  ws_connected; /* cross-thread, guarded by lock */

    /* Worker threads */
    struct claw_thread  *ai_thread;
    struct claw_thread  *out_thread;
    struct claw_thread  *main_thread;

    /* Event dedup ring buffer */
    char                 dedup[DEDUP_SLOTS][EVENT_ID_MAX];
    int                  dedup_idx;

    /* Stale event filter */
    uint64_t             latest_event_ts;

    /* Message queues */
    struct claw_mq      *inbound_q;
    struct claw_mq      *outbound_q;
};

CLAW_ASSERT_EMBEDDED_FIRST(struct feishu_ctx, base);

/* Tentative definition — actual initializer after #endif */
static struct feishu_ctx s_feishu;

/* ------------------------------------------------------------------ */
/*  Pure helpers (no state)                                            */
/* ------------------------------------------------------------------ */

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

static int dedup_check_and_add(struct feishu_ctx *ctx, const char *event_id)
{
    for (int i = 0; i < DEDUP_SLOTS; i++) {
        if (strcmp(ctx->dedup[i], event_id) == 0) {
            return 1; /* duplicate */
        }
    }
    snprintf(ctx->dedup[ctx->dedup_idx], EVENT_ID_MAX, "%s", event_id);
    ctx->dedup_idx = (ctx->dedup_idx + 1) % DEDUP_SLOTS;
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
/*  HTTP helper (platform-independent via OSAL)                        */
/* ------------------------------------------------------------------ */

static int http_post_json(const char *url,
                          const char *auth_header,
                          const char *body,
                          char *resp, size_t resp_size)
{
    claw_net_header_t hdrs[2];
    int hdr_count = 0;
    hdrs[hdr_count].key = "Content-Type";
    hdrs[hdr_count].value = "application/json";
    hdr_count++;
    if (auth_header) {
        hdrs[hdr_count].key = "Authorization";
        hdrs[hdr_count].value = auth_header;
        hdr_count++;
    }

    size_t resp_len = 0;
    int status = claw_net_post(url, hdrs, hdr_count,
                               body, strlen(body),
                               resp, resp_size, &resp_len);

    if (status < 200 || status >= 300) {
        CLAW_LOGE(TAG, "HTTP POST %s: status=%d",
                  url, status);
        return CLAW_ERROR;
    }
    return CLAW_OK;
}

/* ------------------------------------------------------------------ */
/*  Token management (for sending replies via REST API)                */
/* ------------------------------------------------------------------ */

static int refresh_token(struct feishu_ctx *ctx)
{
    char *resp = claw_malloc(RESP_BUF_SIZE);
    if (!resp) {
        return CLAW_NOMEM;
    }

    char body[256];
    snprintf(body, sizeof(body),
             "{\"app_id\":\"%s\",\"app_secret\":\"%s\"}",
             ctx->app_id, ctx->app_secret);

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

    claw_mutex_lock(ctx->lock, CLAW_WAIT_FOREVER);
    snprintf(ctx->token, sizeof(ctx->token), "%s", token->valuestring);
    claw_mutex_unlock(ctx->lock);

    CLAW_LOGI(TAG, "tenant token refreshed");
    cJSON_Delete(root);
    return CLAW_OK;
}

static void get_auth_header(struct feishu_ctx *ctx, char *out, size_t size)
{
    claw_mutex_lock(ctx->lock, CLAW_WAIT_FOREVER);
    snprintf(out, size, "Bearer %s", ctx->token);
    claw_mutex_unlock(ctx->lock);
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
static int send_reply(struct feishu_ctx *ctx,
                      const char *chat_id, const char *text)
{
    char auth[TOKEN_BUF_SIZE + 16];
    get_auth_header(ctx, auth, sizeof(auth));

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

static void add_reaction(struct feishu_ctx *ctx,
                         const char *message_id, const char *emoji_type)
{
    char auth[TOKEN_BUF_SIZE + 16];
    get_auth_header(ctx, auth, sizeof(auth));

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
static void enqueue_reply(struct feishu_ctx *ctx, const char *chat_id,
                          const char *msg_id, const char *text);

/* ------------------------------------------------------------------ */
/*  Scheduled-task reply callback — routes results back to Feishu      */
/* ------------------------------------------------------------------ */

static void sched_reply_to_feishu(const char *target, const char *text)
{
    if (target && target[0] != '\0' && text && text[0] != '\0') {
        enqueue_reply(&s_feishu, target, NULL, text);
    }
}

/* ------------------------------------------------------------------ */
/*  Message bus — inbound queue (WS->AI) + outbound queue (AI->HTTP)   */
/* ------------------------------------------------------------------ */

/* ---- Outbound dispatch thread ---- */

static void outbound_thread(void *arg)
{
    struct feishu_ctx *ctx = arg;
    feishu_outbound_t msg;

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

static void enqueue_reply(struct feishu_ctx *ctx, const char *chat_id,
                          const char *msg_id, const char *text)
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

    if (claw_mq_send(ctx->outbound_q, &msg, sizeof(msg), 1000)
            != CLAW_OK) {
        CLAW_LOGW(TAG, "outbound queue full, dropping reply");
        claw_free(copy);
    }
}

/* ---- AI worker thread ---- */

static void ai_worker_thread(void *arg)
{
    struct feishu_ctx *ctx = arg;
    char *reply = claw_malloc(REPLY_BUF_SIZE);
    if (!reply) {
        CLAW_LOGE(TAG, "worker: no memory");
        return;
    }

    feishu_inbound_t in;

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
                    enqueue_reply(ctx, in.chat_id, in.msg_id, cmd_reply);
                    claw_free(cmd_reply);
                    continue;
                }
                claw_free(cmd_reply);
            }
            /* Not a skill — fall through to ai_chat() */
        }
#endif

        /* Immediate typing indicator — before AI call */
        if (in.msg_id[0] != '\0') {
            add_reaction(ctx, in.msg_id, "Typing");
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
                  (unsigned)claw_tick_ms());
        int ret = ai_chat(in.text, reply, REPLY_BUF_SIZE);
        CLAW_LOGI(TAG, "[%lu ms] ai_chat ret=%d (heap=%u)",
                  (unsigned long)claw_tick_ms(), ret,
                  (unsigned)claw_tick_ms());

        ai_set_channel(AI_CHANNEL_SHELL);
        ai_set_channel_hint(NULL);
        sched_set_reply_context(NULL, NULL);

        if (ret == CLAW_OK && reply[0] != '\0') {
            enqueue_reply(ctx, in.chat_id, in.msg_id, reply);
        } else {
            /*
             * Forward the actual error message from ai_chat()
             * so the user sees what went wrong (e.g. "API request
             * failed", "no API key", "AI is busy").
             */
            if (reply[0] != '\0') {
                enqueue_reply(ctx, in.chat_id, in.msg_id, reply);
            } else {
                enqueue_reply(ctx, in.chat_id, in.msg_id,
                              "[rt-claw] AI engine error");
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Process incoming event payload (JSON inside protobuf frame)        */
/* ------------------------------------------------------------------ */

static void handle_message_event(struct feishu_ctx *ctx, cJSON *event)
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

    if (claw_mq_send(ctx->inbound_q, &in, sizeof(in), 0) != CLAW_OK) {
        CLAW_LOGW(TAG, "[%lu ms] !!! inbound queue full, "
                  "dropping msg_id=%s",
                  (unsigned long)claw_tick_ms(), mid);
    } else {
        CLAW_LOGI(TAG, "[%lu ms] --> queued (depth=%d)",
                  (unsigned long)claw_tick_ms(), INBOUND_DEPTH);
    }

    cJSON_Delete(content);
}

static void process_event_payload(struct feishu_ctx *ctx,
                                  const uint8_t *data, int len)
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
            if (dedup_check_and_add(ctx, eid->valuestring)) {
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
            if (ctx->latest_event_ts > 0 &&
                ts + STALE_EVENT_MAX_AGE_MS < ctx->latest_event_ts) {
                CLAW_LOGW(TAG, "[%lu ms] stale event "
                          "(create_time=%llu, latest=%llu), dropped",
                          (unsigned long)claw_tick_ms(),
                          (unsigned long long)ts,
                          (unsigned long long)ctx->latest_event_ts);
                cJSON_Delete(root);
                return;
            }
            if (ts > ctx->latest_event_ts) {
                ctx->latest_event_ts = ts;
            }
        }

        cJSON *event_type = cJSON_GetObjectItem(header, "event_type");
        if (event_type && cJSON_IsString(event_type)) {
            if (strcmp(event_type->valuestring,
                       "im.message.receive_v1") == 0) {
                cJSON *event = cJSON_GetObjectItem(root, "event");
                if (event) {
                    handle_message_event(ctx, event);
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
/*  WebSocket send helper                                              */
/* ------------------------------------------------------------------ */

static int ws_send_bin(struct feishu_ctx *ctx,
                       const uint8_t *data, int len)
{
    if (len <= 0) {
        return CLAW_ERROR;
    }
#ifdef CLAW_PLATFORM_ESP_IDF
    if (ctx->ws_client) {
        esp_websocket_client_send_bin(
            ctx->ws_client, (const char *)data, len,
            portMAX_DELAY);
    }
#elif defined(CLAW_PLATFORM_LINUX)
    if (ctx->ws_curl) {
        size_t sent = 0;
        curl_ws_send(ctx->ws_curl, data, (size_t)len,
                     &sent, 0, CURLWS_BINARY);
    }
#else
    (void)ctx;
    (void)data;
#endif
    return CLAW_OK;
}

/* Handle a received WebSocket frame (called from event/poll) */
static void ws_handle_frame(struct feishu_ctx *ctx,
                            const uint8_t *data, size_t len,
                            int is_binary)
{
    if (is_binary && len > 0) {
        struct feishu_frame f;
        if (parse_frame(data, len, &f) == 0) {
            if (f.type && f.type_len == 4 &&
                memcmp(f.type, "ping", 4) == 0) {
                uint8_t pong[64];
                int plen = build_pong_frame(pong,
                                            sizeof(pong));
                ws_send_bin(ctx, pong, plen);
                CLAW_LOGD(TAG, "ping/pong");
            } else if (f.method == 1 && f.payload &&
                       f.payload_len > 0) {
                CLAW_LOGI(TAG, "ws DATA frame, "
                    "payload_len=%d", f.payload_len);
                if (f.msg_id && f.msg_id_len > 0) {
                    uint8_t ack[128];
                    int alen = build_ack_frame(
                        ack, sizeof(ack),
                        f.msg_id, f.msg_id_len);
                    ws_send_bin(ctx, ack, alen);
                }
                process_event_payload(ctx, f.payload,
                                      f.payload_len);
            }
        }
    } else if (!is_binary && len > 0) {
        process_event_payload(ctx, data, len);
    }
}

/* ------------------------------------------------------------------ */
/*  WebSocket event handler (platform-specific)                        */
/* ------------------------------------------------------------------ */

#ifdef CLAW_PLATFORM_ESP_IDF

static void ws_event_handler(void *arg, esp_event_base_t base,
                             int32_t event_id, void *event_data)
{
    struct feishu_ctx *ctx = arg;
    (void)base;
    esp_websocket_event_data_t *ws = event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        CLAW_LOGI(TAG, "ws connected");
        ctx->ws_connected = 1;
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        CLAW_LOGW(TAG, "ws disconnected");
        ctx->ws_connected = 0;
        break;
    case WEBSOCKET_EVENT_DATA:
        ws_handle_frame(ctx, (const uint8_t *)ws->data_ptr,
                        ws->data_len,
                        ws->op_code == 0x02);
        break;
    case WEBSOCKET_EVENT_ERROR:
        CLAW_LOGE(TAG, "ws error");
        ctx->ws_connected = 0;
        break;
    default:
        break;
    }
}

#endif /* CLAW_PLATFORM_ESP_IDF */

/* ------------------------------------------------------------------ */
/*  Fetch WebSocket endpoint and connect                               */
/* ------------------------------------------------------------------ */

static int connect_ws(struct feishu_ctx *ctx)
{
    char *resp = claw_malloc(RESP_BUF_SIZE);
    if (!resp) {
        return CLAW_NOMEM;
    }

    char body[256];
    snprintf(body, sizeof(body),
             "{\"AppID\":\"%s\",\"AppSecret\":\"%s\"}",
             ctx->app_id, ctx->app_secret);

    int ret = http_post_json(WS_EP_URL, NULL, body,
                             resp, RESP_BUF_SIZE);
    if (ret != CLAW_OK) {
        CLAW_LOGE(TAG, "ws endpoint request failed");
        claw_free(resp);
        return ret;
    }

    cJSON *root = cJSON_Parse(resp);
    claw_free(resp);
    if (!root) {
        CLAW_LOGE(TAG, "ws endpoint JSON parse failed");
        return CLAW_ERROR;
    }

    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (code && cJSON_IsNumber(code) &&
        code->valueint != 0) {
        cJSON *msg = cJSON_GetObjectItem(root, "msg");
        CLAW_LOGE(TAG, "ws endpoint error: %d %s",
                  code->valueint,
                  (msg && cJSON_IsString(msg))
                      ? msg->valuestring : "?");
        cJSON_Delete(root);
        return CLAW_ERROR;
    }

    cJSON *data_obj = cJSON_GetObjectItem(root, "data");
    cJSON *url_obj = data_obj
        ? cJSON_GetObjectItem(data_obj, "URL") : NULL;
    if (!url_obj || !cJSON_IsString(url_obj)) {
        CLAW_LOGE(TAG, "ws endpoint missing URL");
        cJSON_Delete(root);
        return CLAW_ERROR;
    }

    CLAW_LOGI(TAG, "ws url: %s", url_obj->valuestring);

#ifdef CLAW_PLATFORM_ESP_IDF
    esp_websocket_client_config_t ws_cfg = {
        .uri = url_obj->valuestring,
        .buffer_size = 4096,
        .task_stack = 8192,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .reconnect_timeout_ms = WS_RECONNECT_MS,
        .network_timeout_ms = 10000,
    };

    ctx->ws_client = esp_websocket_client_init(&ws_cfg);
    cJSON_Delete(root);
    if (!ctx->ws_client) {
        return CLAW_ERROR;
    }

    esp_websocket_register_events(ctx->ws_client,
        WEBSOCKET_EVENT_ANY, ws_event_handler, ctx);
    esp_err_t err = esp_websocket_client_start(ctx->ws_client);
    if (err != ESP_OK) {
        CLAW_LOGE(TAG, "ws start failed: %d", err);
        esp_websocket_client_destroy(ctx->ws_client);
        ctx->ws_client = NULL;
        return CLAW_ERROR;
    }
#elif defined(CLAW_PLATFORM_LINUX)
    ctx->ws_curl = curl_easy_init();
    if (!ctx->ws_curl) {
        cJSON_Delete(root);
        return CLAW_ERROR;
    }
    curl_easy_setopt(ctx->ws_curl, CURLOPT_URL,
                     url_obj->valuestring);
    curl_easy_setopt(ctx->ws_curl, CURLOPT_CONNECT_ONLY, 2L);
    CURLcode res = curl_easy_perform(ctx->ws_curl);
    cJSON_Delete(root);
    if (res != CURLE_OK) {
        CLAW_LOGE(TAG, "ws connect failed: %s",
                  curl_easy_strerror(res));
        curl_easy_cleanup(ctx->ws_curl);
        ctx->ws_curl = NULL;
        return CLAW_ERROR;
    }
    ctx->ws_connected = 1;
#else
    cJSON_Delete(root);
    return CLAW_ERROR;
#endif

    return CLAW_OK;
}

/* ------------------------------------------------------------------ */
/*  Main service thread                                                */
/* ------------------------------------------------------------------ */

static void feishu_thread(void *arg)
{
    struct feishu_ctx *ctx = arg;

    CLAW_LOGI(TAG, "service starting (app_id=%s)", ctx->app_id);

    /* Fetch tenant token first (needed for sending replies) */
    int retries = 0;

    while (refresh_token(ctx) != CLAW_OK) {
        if (claw_thread_should_exit()) {
            return;
        }
        retries++;
        if (retries >= TOKEN_MAX_RETRIES) {
            CLAW_LOGE(TAG, "token failed after %d retries",
                      retries);
            return;
        }
        CLAW_LOGW(TAG, "token failed, retry in 10s");
        claw_thread_delay_ms(10000);
    }
    if (claw_thread_should_exit()) {
        return;
    }
    CLAW_LOGI(TAG, "token acquired");

    /* Connect WebSocket long connection */
    while (connect_ws(ctx) != CLAW_OK) {
        if (claw_thread_should_exit()) {
            return;
        }
        CLAW_LOGW(TAG, "ws connect failed, retry in %dms",
                  WS_RECONNECT_MS);
        claw_thread_delay_ms(WS_RECONNECT_MS);
    }
    CLAW_LOGI(TAG, "ws connected — ready to receive messages");

    /* Keep-alive loop */
    uint32_t last_refresh = claw_tick_ms();

    while (!claw_thread_should_exit()) {
#ifdef CLAW_PLATFORM_LINUX
        /* Linux: poll WebSocket for incoming frames */
        if (ctx->ws_curl && ctx->ws_connected) {
            const struct curl_ws_frame *meta = NULL;
            uint8_t buf[4096];
            size_t nread = 0;
            CURLcode rc = curl_ws_recv(ctx->ws_curl, buf,
                sizeof(buf), &nread, &meta);
            if (rc == CURLE_OK && nread > 0 && meta) {
                int binary = (meta->flags & CURLWS_BINARY);
                ws_handle_frame(ctx, buf, nread, binary);
            } else if (rc == CURLE_AGAIN) {
                claw_thread_delay_ms(100);
            } else if (rc != CURLE_OK) {
                ctx->ws_connected = 0;
            }
        } else {
            claw_thread_delay_ms(1000);
        }
#else
        claw_thread_delay_ms(5000);
#endif

        /* Periodic token refresh */
        if (claw_tick_ms() - last_refresh >= TOKEN_REFRESH_MS) {
            refresh_token(ctx);
            last_refresh = claw_tick_ms();
        }

        /* Reconnect on disconnect */
        if (!ctx->ws_connected) {
            CLAW_LOGW(TAG, "ws lost, reconnecting...");
#ifdef CLAW_PLATFORM_ESP_IDF
            if (ctx->ws_client) {
                esp_websocket_client_destroy(ctx->ws_client);
                ctx->ws_client = NULL;
            }
#elif defined(CLAW_PLATFORM_LINUX)
            if (ctx->ws_curl) {
                curl_easy_cleanup(ctx->ws_curl);
                ctx->ws_curl = NULL;
            }
#endif
            claw_thread_delay_ms(WS_RECONNECT_MS);
            connect_ws(ctx);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Public API (via static singleton)                                  */
/* ------------------------------------------------------------------ */

void feishu_set_app_id(const char *app_id)
{
    if (app_id) {
        snprintf(s_feishu.app_id, sizeof(s_feishu.app_id),
                 "%s", app_id);
    }
}

void feishu_set_app_secret(const char *app_secret)
{
    if (app_secret) {
        snprintf(s_feishu.app_secret, sizeof(s_feishu.app_secret),
                 "%s", app_secret);
    }
}

const char *feishu_get_app_id(void)     { return s_feishu.app_id; }
const char *feishu_get_app_secret(void) { return s_feishu.app_secret; }

/* ------------------------------------------------------------------ */
/*  OOP lifecycle ops                                                  */
/* ------------------------------------------------------------------ */

static claw_err_t feishu_svc_init(struct claw_service *svc)
{
    struct feishu_ctx *ctx = container_of(svc, struct feishu_ctx, base);

    /* Initialize from compile-time defaults if not set via setter */
    if (ctx->app_id[0] == '\0') {
        snprintf(ctx->app_id, sizeof(ctx->app_id),
                 "%s", CONFIG_RTCLAW_FEISHU_APP_ID);
    }
    if (ctx->app_secret[0] == '\0') {
        snprintf(ctx->app_secret, sizeof(ctx->app_secret),
                 "%s", CONFIG_RTCLAW_FEISHU_APP_SECRET);
    }

    if (ctx->app_id[0] == '\0' || ctx->app_secret[0] == '\0') {
        CLAW_LOGE(TAG, "no credentials configured");
        return CLAW_ERR_GENERIC;
    }

    ctx->lock = claw_mutex_create("feishu");
    if (!ctx->lock) {
        return CLAW_ERR_GENERIC;
    }

    ctx->inbound_q = claw_mq_create("fs_in",
                                     sizeof(feishu_inbound_t),
                                     INBOUND_DEPTH);
    ctx->outbound_q = claw_mq_create("fs_out",
                                      sizeof(feishu_outbound_t),
                                      OUTBOUND_DEPTH);
    if (!ctx->inbound_q || !ctx->outbound_q) {
        CLAW_LOGE(TAG, "message queue create failed");
        return CLAW_ERR_NOMEM;
    }

    ctx->token[0] = '\0';
#ifdef CLAW_PLATFORM_ESP_IDF
    ctx->ws_client = NULL;
#elif defined(CLAW_PLATFORM_LINUX)
    ctx->ws_curl = NULL;
#endif
    ctx->ws_connected = 0;
    CLAW_LOGI(TAG, "init ok");
    return CLAW_OK;
}

static claw_err_t feishu_svc_start(struct claw_service *svc)
{
    struct feishu_ctx *ctx = container_of(svc, struct feishu_ctx, base);

    /* AI worker: inbound queue -> ai_chat -> outbound queue */
    ctx->ai_thread = claw_thread_create("fs_ai", ai_worker_thread,
                                         ctx, WORKER_STACK, 10);
    if (!ctx->ai_thread) {
        CLAW_LOGE(TAG, "failed to create AI worker");
        return CLAW_ERR_GENERIC;
    }

    /* Outbound dispatch: outbound queue -> send_reply (HTTP) */
    ctx->out_thread = claw_thread_create("fs_out", outbound_thread,
                                          ctx, OUTBOUND_STACK, 10);
    if (!ctx->out_thread) {
        CLAW_LOGE(TAG, "failed to create outbound thread");
        return CLAW_ERR_GENERIC;
    }

    /* Feishu WebSocket connection thread */
    ctx->main_thread = claw_thread_create("feishu", feishu_thread,
                                           ctx, 8192, 10);
    if (!ctx->main_thread) {
        CLAW_LOGE(TAG, "failed to create thread");
        return CLAW_ERR_GENERIC;
    }
    return CLAW_OK;
}

static void feishu_svc_stop(struct claw_service *svc)
{
    struct feishu_ctx *ctx = container_of(svc, struct feishu_ctx, base);

    /*
     * Signal disconnected so main thread's recv loop
     * exits on its next timeout cycle, then join threads
     * before destroying handles.
     */
    ctx->ws_connected = 0;

#ifdef CLAW_PLATFORM_ESP_IDF
    /* ESP-IDF WS client has its own stop that unblocks */
    if (ctx->ws_client) {
        esp_websocket_client_stop(ctx->ws_client);
    }
#endif

    /* Join threads first (they check exit flag) */
    claw_thread_delete(ctx->main_thread);
    ctx->main_thread = NULL;

    claw_thread_delete(ctx->ai_thread);
    ctx->ai_thread = NULL;

    claw_thread_delete(ctx->out_thread);
    ctx->out_thread = NULL;

    /* Now safe to destroy handles — no thread using them */
#ifdef CLAW_PLATFORM_ESP_IDF
    if (ctx->ws_client) {
        esp_websocket_client_destroy(ctx->ws_client);
        ctx->ws_client = NULL;
    }
#elif defined(CLAW_PLATFORM_LINUX)
    if (ctx->ws_curl) {
        curl_easy_cleanup(ctx->ws_curl);
        ctx->ws_curl = NULL;
    }
#endif

    if (ctx->inbound_q) {
        claw_mq_delete(ctx->inbound_q);
        ctx->inbound_q = NULL;
    }
    if (ctx->outbound_q) {
        claw_mq_delete(ctx->outbound_q);
        ctx->outbound_q = NULL;
    }
    if (ctx->lock) {
        claw_mutex_delete(ctx->lock);
        ctx->lock = NULL;
    }

    CLAW_LOGI(TAG, "stopped");
}

#else /* !CONFIG_RTCLAW_FEISHU_ENABLE */

/* Minimal context when Feishu is disabled */
struct feishu_ctx {
    struct claw_service base;
};

CLAW_ASSERT_EMBEDDED_FIRST(struct feishu_ctx, base);

static struct feishu_ctx s_feishu;

void feishu_set_app_id(const char *id)     { (void)id; }
void feishu_set_app_secret(const char *s)  { (void)s; }
const char *feishu_get_app_id(void)        { return ""; }
const char *feishu_get_app_secret(void)    { return ""; }

static claw_err_t feishu_svc_init(struct claw_service *svc)
{
    (void)svc;
    return CLAW_OK;
}

static claw_err_t feishu_svc_start(struct claw_service *svc)
{
    (void)svc;
    return CLAW_OK;
}

static void feishu_svc_stop(struct claw_service *svc)
{
    (void)svc;
}

#endif /* CONFIG_RTCLAW_FEISHU_ENABLE */

/* ------------------------------------------------------------------ */
/*  OOP service registration                                           */
/* ------------------------------------------------------------------ */

static const char *feishu_deps[] = { "ai_engine", NULL };

static const struct claw_service_ops feishu_svc_ops = {
    .init  = feishu_svc_init,
    .start = feishu_svc_start,
    .stop  = feishu_svc_stop,
};

static struct feishu_ctx s_feishu = {
    .base = {
        .name  = "feishu",
        .ops   = &feishu_svc_ops,
        .deps  = feishu_deps,
        .state = CLAW_SVC_CREATED,
    },
};

CLAW_SERVICE_REGISTER(feishu, &s_feishu.base);
