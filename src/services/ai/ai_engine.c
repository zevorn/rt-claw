/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 *
 * AI engine — LLM API client with Tool Use support and conversation memory.
 */

#include "claw_os.h"
#include "claw_config.h"
#include "ai_engine.h"
#include "ai_memory.h"
#include "claw_tools.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "ai"

/* Platform-specific HTTP transport */
#ifdef CLAW_PLATFORM_ESP_IDF
#include "sdkconfig.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#elif defined(CLAW_PLATFORM_RTTHREAD)
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#endif

#define AI_API_KEY      CONFIG_CLAW_AI_API_KEY
#define AI_API_URL      CONFIG_CLAW_AI_API_URL
#define AI_MODEL        CONFIG_CLAW_AI_MODEL
#define AI_MAX_TOKENS   CONFIG_CLAW_AI_MAX_TOKENS

#define RESP_BUF_SIZE      8192
#define MAX_TOOL_ROUNDS    5
#define API_MAX_RETRIES    3
#define API_RETRY_BASE_MS  3000

static claw_mutex_t s_api_lock;
static ai_status_cb_t s_status_cb;
static char s_channel_hint[128];

static inline void notify_status(int st, const char *detail)
{
    if (s_status_cb) {
        s_status_cb(st, detail);
    }
}

void ai_set_status_cb(ai_status_cb_t cb)
{
    s_status_cb = cb;
}

void ai_set_channel_hint(const char *hint)
{
    if (hint) {
        snprintf(s_channel_hint, sizeof(s_channel_hint), "%s", hint);
    } else {
        s_channel_hint[0] = '\0';
    }
}

static const char *SYSTEM_PROMPT =
    "You are rt-claw, an AI assistant running on an embedded RTOS device. "
    "You can control hardware peripherals (GPIO, sensors, etc.) through tools. "
    "When the user asks to control hardware, use the appropriate tool. "
    "Be concise — this is an embedded device with limited display. "
    "Respond in the same language the user uses.";

/* HTTP response collection context */
typedef struct {
    char *buf;
    size_t size;
    size_t len;
} resp_ctx_t;

static int is_retryable_status(int status)
{
    return status == 429 || status == 500 || status == 502 ||
           status == 503 || status == 529;
}

/* ---- Platform-specific HTTP POST ---- */

#ifdef CLAW_PLATFORM_ESP_IDF

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    resp_ctx_t *ctx = (resp_ctx_t *)evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx) {
        size_t avail = ctx->size - ctx->len - 1;
        size_t copy = ((size_t)evt->data_len < avail)
                      ? (size_t)evt->data_len : avail;
        if (copy > 0) {
            memcpy(ctx->buf + ctx->len, evt->data, copy);
            ctx->len += copy;
            ctx->buf[ctx->len] = '\0';
        }
    }
    return ESP_OK;
}

static cJSON *do_api_call(cJSON *req_body, resp_ctx_t *ctx)
{
    char *body_str = cJSON_PrintUnformatted(req_body);
    if (!body_str) {
        return NULL;
    }

    for (int attempt = 0; attempt <= API_MAX_RETRIES; attempt++) {
        if (attempt > 0) {
            int delay = API_RETRY_BASE_MS * attempt;
            CLAW_LOGW(TAG, "retry %d/%d in %dms ...",
                      attempt, API_MAX_RETRIES, delay);
            claw_lcd_status("Retrying ...");
            claw_thread_delay_ms(delay);
        }

        ctx->len = 0;
        ctx->buf[0] = '\0';

        esp_http_client_config_t http_cfg = {
            .url = AI_API_URL,
            .method = HTTP_METHOD_POST,
            .timeout_ms = 60000,
            .event_handler = on_http_event,
            .user_data = ctx,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .buffer_size = 4096,
            .buffer_size_tx = 4096,
        };

        esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
        if (!client) {
            continue;
        }

        esp_http_client_set_header(client, "Content-Type",
                                   "application/json");
        esp_http_client_set_header(client, "x-api-key", AI_API_KEY);
        esp_http_client_set_header(client, "anthropic-version",
                                   "2023-06-01");
        esp_http_client_set_post_field(client, body_str, strlen(body_str));

        esp_err_t err = esp_http_client_perform(client);
        int status = 0;
        if (err == ESP_OK) {
            status = esp_http_client_get_status_code(client);
        }
        esp_http_client_cleanup(client);

        if (err != ESP_OK) {
            CLAW_LOGE(TAG, "HTTP failed: %s", esp_err_to_name(err));
            continue;
        }

        if (is_retryable_status(status)) {
            CLAW_LOGW(TAG, "HTTP %d (transient)", status);
            continue;
        }

        if (status != 200) {
            CLAW_LOGE(TAG, "HTTP %d: %.200s", status, ctx->buf);
            cJSON_free(body_str);
            return NULL;
        }

        cJSON_free(body_str);
        return cJSON_Parse(ctx->buf);
    }

    CLAW_LOGE(TAG, "API call failed after %d retries", API_MAX_RETRIES);
    cJSON_free(body_str);
    return NULL;
}

#elif defined(CLAW_PLATFORM_RTTHREAD)

/*
 * Minimal HTTP POST via BSD socket.
 * Supports HTTP only (no TLS). For HTTPS endpoints, use an HTTP proxy
 * or a reverse-proxy with TLS termination.
 *
 * URL format: http://host[:port]/path
 */

/* Parse URL into host, port, path components */
static int parse_url(const char *url, char *host, size_t host_sz,
                     int *port, char *path, size_t path_sz)
{
    const char *p = url;

    /* Detect scheme */
    if (strncmp(p, "https://", 8) == 0) {
        *port = 443;
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        *port = 80;
        p += 7;
    } else {
        return -1;
    }

    /* Extract host */
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');
    size_t host_len;

    if (colon && (!slash || colon < slash)) {
        host_len = colon - p;
        *port = atoi(colon + 1);
    } else if (slash) {
        host_len = slash - p;
    } else {
        host_len = strlen(p);
    }

    if (host_len >= host_sz) {
        host_len = host_sz - 1;
    }
    memcpy(host, p, host_len);
    host[host_len] = '\0';

    /* Extract path */
    if (slash) {
        snprintf(path, path_sz, "%s", slash);
    } else {
        snprintf(path, path_sz, "/");
    }

    return 0;
}

/* Read HTTP response, skip headers, return body in ctx */
static int http_recv_response(int sock, resp_ctx_t *ctx, int *status_out)
{
    char tmp[512];
    int total = 0;
    int header_done = 0;
    char *body_start = NULL;
    int content_length = -1;
    int body_received = 0;

    *status_out = 0;
    ctx->len = 0;
    ctx->buf[0] = '\0';

    /* Read response in chunks */
    while (1) {
        int n = recv(sock, tmp, sizeof(tmp) - 1, 0);
        if (n <= 0) {
            break;
        }
        tmp[n] = '\0';

        if (!header_done) {
            /* Accumulate into ctx->buf until we find \r\n\r\n */
            size_t avail = ctx->size - ctx->len - 1;
            size_t copy = ((size_t)n < avail) ? (size_t)n : avail;
            memcpy(ctx->buf + ctx->len, tmp, copy);
            ctx->len += copy;
            ctx->buf[ctx->len] = '\0';

            body_start = strstr(ctx->buf, "\r\n\r\n");
            if (body_start) {
                header_done = 1;
                body_start += 4;

                /* Parse status from first line */
                if (sscanf(ctx->buf, "HTTP/%*d.%*d %d", status_out) != 1) {
                    *status_out = 0;
                }

                /* Try to find Content-Length */
                char *cl = strstr(ctx->buf, "Content-Length:");
                if (!cl) {
                    cl = strstr(ctx->buf, "content-length:");
                }
                if (cl) {
                    content_length = atoi(cl + 15);
                }

                /* Move body to start of buffer */
                size_t body_len = ctx->len - (body_start - ctx->buf);
                memmove(ctx->buf, body_start, body_len);
                ctx->len = body_len;
                ctx->buf[ctx->len] = '\0';
                body_received = (int)body_len;
            }
        } else {
            /* Already past headers, append body data */
            size_t avail = ctx->size - ctx->len - 1;
            size_t copy = ((size_t)n < avail) ? (size_t)n : avail;
            memcpy(ctx->buf + ctx->len, tmp, copy);
            ctx->len += copy;
            ctx->buf[ctx->len] = '\0';
            body_received += n;
        }

        total += n;

        /* Check if we have all the body */
        if (header_done && content_length >= 0 &&
            body_received >= content_length) {
            break;
        }
    }

    return header_done ? 0 : -1;
}

static cJSON *do_api_call(cJSON *req_body, resp_ctx_t *ctx)
{
    char *body_str = cJSON_PrintUnformatted(req_body);
    if (!body_str) {
        return NULL;
    }

    char host[128];
    char path[256];
    int port;

    if (parse_url(AI_API_URL, host, sizeof(host),
                  &port, path, sizeof(path)) < 0) {
        CLAW_LOGE(TAG, "invalid API URL: %s", AI_API_URL);
        cJSON_free(body_str);
        return NULL;
    }

    /* Warn if HTTPS — no TLS support on this platform */
    if (port == 443) {
        CLAW_LOGW(TAG, "HTTPS not supported, trying plain HTTP on port 443");
    }

    for (int attempt = 0; attempt <= API_MAX_RETRIES; attempt++) {
        if (attempt > 0) {
            int delay = API_RETRY_BASE_MS * attempt;
            CLAW_LOGW(TAG, "retry %d/%d in %dms ...",
                      attempt, API_MAX_RETRIES, delay);
            claw_thread_delay_ms(delay);
        }

        /* DNS resolve */
        struct hostent *he = gethostbyname(host);
        if (!he) {
            CLAW_LOGE(TAG, "DNS resolve failed: %s", host);
            continue;
        }

        /* Create socket */
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            CLAW_LOGE(TAG, "socket create failed");
            continue;
        }

        /* Set timeout */
        struct timeval tv;
        tv.tv_sec = 60;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        /* Connect */
        struct sockaddr_in server;
        memset(&server, 0, sizeof(server));
        server.sin_family = AF_INET;
        server.sin_port = htons(port);
        memcpy(&server.sin_addr, he->h_addr, he->h_length);

        if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
            CLAW_LOGE(TAG, "connect failed: %s:%d", host, port);
            close(sock);
            continue;
        }

        /* Build HTTP request */
        size_t body_len = strlen(body_str);
        char header[512];
        int hdr_len = snprintf(header, sizeof(header),
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Content-Type: application/json\r\n"
            "x-api-key: %s\r\n"
            "anthropic-version: 2023-06-01\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n",
            path, host, AI_API_KEY, (int)body_len);

        /* Send header + body */
        if (send(sock, header, hdr_len, 0) < 0 ||
            send(sock, body_str, body_len, 0) < 0) {
            CLAW_LOGE(TAG, "send failed");
            close(sock);
            continue;
        }

        /* Read response */
        int status = 0;
        int rc = http_recv_response(sock, ctx, &status);
        close(sock);

        if (rc < 0) {
            CLAW_LOGE(TAG, "HTTP response parse failed");
            continue;
        }

        if (is_retryable_status(status)) {
            CLAW_LOGW(TAG, "HTTP %d (transient)", status);
            continue;
        }

        if (status != 200) {
            CLAW_LOGE(TAG, "HTTP %d: %.200s", status, ctx->buf);
            cJSON_free(body_str);
            return NULL;
        }

        cJSON_free(body_str);
        return cJSON_Parse(ctx->buf);
    }

    CLAW_LOGE(TAG, "API call failed after %d retries", API_MAX_RETRIES);
    cJSON_free(body_str);
    return NULL;
}

#else /* no HTTP transport */

static cJSON *do_api_call(cJSON *req_body, resp_ctx_t *ctx)
{
    (void)req_body;
    (void)ctx;
    return NULL;
}

#endif /* platform-specific HTTP */

/* ---- Shared AI engine logic ---- */

static cJSON *build_request(const char *system_prompt,
                            cJSON *messages, cJSON *tools)
{
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "model", AI_MODEL);
    cJSON_AddNumberToObject(req, "max_tokens", AI_MAX_TOKENS);
    cJSON_AddStringToObject(req, "system", system_prompt);

    cJSON_AddItemReferenceToObject(req, "messages", messages);
    if (tools && cJSON_GetArraySize(tools) > 0) {
        cJSON_AddItemReferenceToObject(req, "tools", tools);
    }

    return req;
}

static char *build_system_prompt(void)
{
    size_t base_len = strlen(SYSTEM_PROMPT);
    size_t hint_len = strlen(s_channel_hint);
    char *ltm_ctx = ai_ltm_build_context();
    size_t ltm_len = ltm_ctx ? strlen(ltm_ctx) : 0;

    char *p = claw_malloc(base_len + hint_len + ltm_len + 1);
    if (p) {
        memcpy(p, SYSTEM_PROMPT, base_len);
        if (hint_len > 0) {
            memcpy(p + base_len, s_channel_hint, hint_len);
        }
        if (ltm_len > 0) {
            memcpy(p + base_len + hint_len, ltm_ctx, ltm_len);
        }
        p[base_len + hint_len + ltm_len] = '\0';
    }
    claw_free(ltm_ctx);
    return p;
}

static void extract_text(const cJSON *content, char *reply,
                         size_t reply_size)
{
    size_t offset = 0;

    for (int i = 0; i < cJSON_GetArraySize(content); i++) {
        cJSON *block = cJSON_GetArrayItem(content, i);
        cJSON *type = cJSON_GetObjectItem(block, "type");
        if (!type || !cJSON_IsString(type)) {
            continue;
        }
        if (strcmp(type->valuestring, "text") == 0) {
            cJSON *text = cJSON_GetObjectItem(block, "text");
            if (text && cJSON_IsString(text)) {
                size_t avail = reply_size - offset - 1;
                if (avail > 0) {
                    size_t n = snprintf(reply + offset, avail + 1,
                                        "%s", text->valuestring);
                    offset += (n < avail) ? n : avail;
                }
            }
        }
    }
    if (offset == 0 && reply_size > 0) {
        reply[0] = '\0';
    }
}

static cJSON *execute_tool_calls(const cJSON *content)
{
    cJSON *results = cJSON_CreateArray();

    for (int i = 0; i < cJSON_GetArraySize(content); i++) {
        cJSON *block = cJSON_GetArrayItem(content, i);
        cJSON *type = cJSON_GetObjectItem(block, "type");
        if (!type || strcmp(type->valuestring, "tool_use") != 0) {
            continue;
        }

        cJSON *id = cJSON_GetObjectItem(block, "id");
        cJSON *name = cJSON_GetObjectItem(block, "name");
        cJSON *input = cJSON_GetObjectItem(block, "input");

        const char *tool_name = name ? name->valuestring : "unknown";
        const char *tool_id = id ? id->valuestring : "";

        CLAW_LOGI(TAG, "tool_use: %s", tool_name);
        notify_status(AI_STATUS_TOOL_CALL, tool_name);

        cJSON *result_obj = cJSON_CreateObject();
        const claw_tool_t *tool = claw_tool_find(tool_name);

        if (tool) {
            tool->execute(input, result_obj);
        } else {
            cJSON_AddStringToObject(result_obj, "error",
                                    "tool not found");
            CLAW_LOGE(TAG, "unknown tool: %s", tool_name);
        }

        char *result_str = cJSON_PrintUnformatted(result_obj);
        cJSON_Delete(result_obj);

        cJSON *tr = cJSON_CreateObject();
        cJSON_AddStringToObject(tr, "type", "tool_result");
        cJSON_AddStringToObject(tr, "tool_use_id", tool_id);
        cJSON_AddStringToObject(tr, "content",
                                result_str ? result_str : "{}");
        cJSON_AddItemToArray(results, tr);

        if (result_str) {
            cJSON_free(result_str);
        }
    }
    return results;
}

static int ai_chat_with_messages(const char *system_prompt,
                                 cJSON *messages, cJSON *tools,
                                 char *reply, size_t reply_size)
{
    char *resp_buf = claw_malloc(RESP_BUF_SIZE);
    if (!resp_buf) {
        CLAW_LOGE(TAG, "no memory for response buffer");
        return CLAW_ERROR;
    }

    resp_ctx_t ctx = { .buf = resp_buf, .size = RESP_BUF_SIZE, .len = 0 };
    int ret = CLAW_ERROR;
    reply[0] = '\0';

    claw_lcd_status("Thinking ...");
    claw_lcd_progress(0);
    notify_status(AI_STATUS_THINKING, NULL);

    for (int round = 0; round < MAX_TOOL_ROUNDS; round++) {
        cJSON *req = build_request(system_prompt, messages, tools);
        cJSON *resp = do_api_call(req, &ctx);
        cJSON_Delete(req);

        if (!resp) {
            snprintf(reply, reply_size, "[API request failed]");
            claw_lcd_status("API request failed");
            claw_lcd_progress(0);
            break;
        }

        cJSON *err_obj = cJSON_GetObjectItem(resp, "error");
        if (err_obj) {
            cJSON *err_msg = cJSON_GetObjectItem(err_obj, "message");
            if (err_msg && cJSON_IsString(err_msg)) {
                snprintf(reply, reply_size, "[API error: %s]",
                         err_msg->valuestring);
            }
            cJSON_Delete(resp);
            break;
        }

        cJSON *content = cJSON_GetObjectItem(resp, "content");
        cJSON *stop = cJSON_GetObjectItem(resp, "stop_reason");
        const char *stop_reason = (stop && cJSON_IsString(stop))
                                  ? stop->valuestring : "";

        extract_text(content, reply, reply_size);

        if (strcmp(stop_reason, "tool_use") == 0) {
            CLAW_LOGI(TAG, "tool_use round %d", round + 1);
            claw_lcd_status("Tool call ...");
            claw_lcd_progress((round + 1) * 100 / MAX_TOOL_ROUNDS);

            cJSON *asst_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(asst_msg, "role", "assistant");
            cJSON_AddItemToObject(asst_msg, "content",
                                  cJSON_Duplicate(content, 1));
            cJSON_AddItemToArray(messages, asst_msg);

            cJSON *tool_results = execute_tool_calls(content);
            cJSON *result_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(result_msg, "role", "user");
            cJSON_AddItemToObject(result_msg, "content", tool_results);
            cJSON_AddItemToArray(messages, result_msg);

            cJSON_Delete(resp);
            notify_status(AI_STATUS_THINKING, NULL);
            continue;
        }

        ret = CLAW_OK;
        claw_lcd_status("Done");
        claw_lcd_progress(100);
        cJSON_Delete(resp);
        break;
    }

    notify_status(AI_STATUS_DONE, NULL);
    claw_free(resp_buf);
    return ret;
}

int ai_chat(const char *user_msg, char *reply, size_t reply_size)
{
    if (!user_msg || !reply || reply_size == 0) {
        return CLAW_ERROR;
    }

    if (strlen(AI_API_KEY) == 0) {
        snprintf(reply, reply_size, "[no API key configured]");
        return CLAW_ERROR;
    }

    if (claw_mutex_lock(s_api_lock, 5000) != CLAW_OK) {
        snprintf(reply, reply_size,
                 "[AI is busy processing another task, please retry]");
        return CLAW_ERROR;
    }

    ai_memory_add_message("user", user_msg);

    char *sys_prompt = build_system_prompt();
    if (!sys_prompt) {
        claw_mutex_unlock(s_api_lock);
        return CLAW_ERROR;
    }

    cJSON *messages = ai_memory_build_messages();
    cJSON *tools = claw_tools_to_json();

    int orig_msg_count = cJSON_GetArraySize(messages);

    int ret = ai_chat_with_messages(sys_prompt, messages, tools,
                                     reply, reply_size);

    int msg_count = cJSON_GetArraySize(messages);
    for (int i = orig_msg_count; i < msg_count; i++) {
        cJSON *msg = cJSON_GetArrayItem(messages, i);
        cJSON *role = cJSON_GetObjectItem(msg, "role");
        cJSON *content = cJSON_GetObjectItem(msg, "content");
        if (!role || !content) {
            continue;
        }
        char *content_str = cJSON_PrintUnformatted(content);
        if (content_str) {
            ai_memory_add_message(role->valuestring, content_str);
            cJSON_free(content_str);
        }
    }

    if (ret == CLAW_OK && reply[0] != '\0') {
        ai_memory_add_message("assistant", reply);
    }

    claw_free(sys_prompt);
    cJSON_Delete(messages);
    if (tools) {
        cJSON_Delete(tools);
    }

    claw_mutex_unlock(s_api_lock);
    return ret;
}

int ai_chat_raw(const char *prompt, char *reply, size_t reply_size)
{
    if (!prompt || !reply || reply_size == 0) {
        return CLAW_ERROR;
    }

    if (strlen(AI_API_KEY) == 0) {
        snprintf(reply, reply_size, "[no API key configured]");
        return CLAW_ERROR;
    }

    if (claw_mutex_lock(s_api_lock, 5000) != CLAW_OK) {
        snprintf(reply, reply_size,
                 "[AI is busy processing another task, please retry]");
        return CLAW_ERROR;
    }

    char *sys_prompt = build_system_prompt();
    if (!sys_prompt) {
        claw_mutex_unlock(s_api_lock);
        return CLAW_ERROR;
    }

    cJSON *messages = cJSON_CreateArray();
    cJSON *user_m = cJSON_CreateObject();
    cJSON_AddStringToObject(user_m, "role", "user");
    cJSON_AddStringToObject(user_m, "content", prompt);
    cJSON_AddItemToArray(messages, user_m);

    /*
     * Exclude LCD tools — raw calls run in background contexts
     * (scheduled tasks, skills) where MMIO framebuffer writes
     * would block for minutes on QEMU.
     */
    cJSON *tools = claw_tools_to_json_exclude("lcd_");

    int ret = ai_chat_with_messages(sys_prompt, messages, tools,
                                     reply, reply_size);

    claw_free(sys_prompt);
    cJSON_Delete(messages);
    if (tools) {
        cJSON_Delete(tools);
    }

    claw_mutex_unlock(s_api_lock);
    return ret;
}

int ai_engine_init(void)
{
    s_api_lock = claw_mutex_create("ai_api");
    ai_memory_init();
    ai_ltm_init();

    if (strlen(AI_API_KEY) == 0) {
        CLAW_LOGW(TAG, "no API key configured");
        claw_lcd_status("No API key configured");
    } else {
        CLAW_LOGI(TAG, "engine ready (model: %s, tools: %d)",
                  AI_MODEL, claw_tools_count());
        claw_lcd_status("AI ready - waiting for input");
    }
    return CLAW_OK;
}
