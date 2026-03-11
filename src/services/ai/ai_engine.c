/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 *
 * AI engine — LLM API client (Claude Messages API).
 */

#include "claw_os.h"
#include "ai_engine.h"

#include <string.h>
#include <stdio.h>

#define TAG "ai"

#ifdef CLAW_PLATFORM_ESP_IDF

#include "sdkconfig.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"

#define AI_API_KEY      CONFIG_CLAW_AI_API_KEY
#define AI_API_URL      CONFIG_CLAW_AI_API_URL
#define AI_MODEL        CONFIG_CLAW_AI_MODEL
#define AI_MAX_TOKENS   CONFIG_CLAW_AI_MAX_TOKENS

/* HTTP response collection context */
typedef struct {
    char *buf;
    size_t size;
    size_t len;
} resp_ctx_t;

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    resp_ctx_t *ctx = (resp_ctx_t *)evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx) {
        size_t avail = ctx->size - ctx->len - 1;
        size_t copy = (evt->data_len < avail) ? evt->data_len : avail;
        if (copy > 0) {
            memcpy(ctx->buf + ctx->len, evt->data, copy);
            ctx->len += copy;
            ctx->buf[ctx->len] = '\0';
        }
    }
    return ESP_OK;
}

static char *build_request_body(const char *user_msg)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }

    cJSON_AddStringToObject(root, "model", AI_MODEL);
    cJSON_AddNumberToObject(root, "max_tokens", AI_MAX_TOKENS);

    cJSON *messages = cJSON_AddArrayToObject(root, "messages");
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON_AddStringToObject(msg, "content", user_msg);
    cJSON_AddItemToArray(messages, msg);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return body;
}

static int parse_response(const char *json_str, char *reply,
                          size_t reply_size)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        CLAW_LOGE(TAG, "JSON parse failed");
        return CLAW_ERROR;
    }

    /* Check for API error */
    cJSON *err_obj = cJSON_GetObjectItem(root, "error");
    if (err_obj) {
        cJSON *err_msg = cJSON_GetObjectItem(err_obj, "message");
        if (err_msg && cJSON_IsString(err_msg)) {
            CLAW_LOGE(TAG, "API error: %s", err_msg->valuestring);
        }
        cJSON_Delete(root);
        return CLAW_ERROR;
    }

    /* Extract text from content[0].text */
    cJSON *content = cJSON_GetObjectItem(root, "content");
    if (!content || !cJSON_IsArray(content) ||
        cJSON_GetArraySize(content) == 0) {
        CLAW_LOGE(TAG, "no content in response");
        cJSON_Delete(root);
        return CLAW_ERROR;
    }

    cJSON *first = cJSON_GetArrayItem(content, 0);
    cJSON *text = cJSON_GetObjectItem(first, "text");
    if (!text || !cJSON_IsString(text)) {
        CLAW_LOGE(TAG, "no text in content[0]");
        cJSON_Delete(root);
        return CLAW_ERROR;
    }

    snprintf(reply, reply_size, "%s", text->valuestring);
    cJSON_Delete(root);
    return CLAW_OK;
}

int ai_chat(const char *user_msg, char *reply, size_t reply_size)
{
    if (!user_msg || !reply || reply_size == 0) {
        return CLAW_ERROR;
    }

    if (strlen(AI_API_KEY) == 0) {
        snprintf(reply, reply_size,
                 "[no API key configured — set via idf.py menuconfig]");
        return CLAW_ERROR;
    }

    char *body = build_request_body(user_msg);
    if (!body) {
        CLAW_LOGE(TAG, "failed to build request JSON");
        return CLAW_ERROR;
    }

    /* Allocate response buffer (8KB) */
    size_t resp_buf_size = 8192;
    char *resp_buf = claw_malloc(resp_buf_size);
    if (!resp_buf) {
        cJSON_free(body);
        CLAW_LOGE(TAG, "no memory for response buffer");
        return CLAW_ERROR;
    }
    resp_buf[0] = '\0';

    resp_ctx_t ctx = {
        .buf  = resp_buf,
        .size = resp_buf_size,
        .len  = 0,
    };

    esp_http_client_config_t http_cfg = {
        .url = AI_API_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 60000,
        .event_handler = on_http_event,
        .user_data = &ctx,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        cJSON_free(body);
        claw_free(resp_buf);
        CLAW_LOGE(TAG, "http client init failed");
        return CLAW_ERROR;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "x-api-key", AI_API_KEY);
    esp_http_client_set_header(client, "anthropic-version", "2023-06-01");
    esp_http_client_set_post_field(client, body, strlen(body));

    CLAW_LOGD(TAG, "sending request to %s ...", AI_API_URL);
    esp_err_t err = esp_http_client_perform(client);

    int ret = CLAW_ERROR;
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        CLAW_LOGD(TAG, "HTTP status=%d, body_len=%d", status, (int)ctx.len);
        if (status == 200) {
            ret = parse_response(ctx.buf, reply, reply_size);
        } else {
            CLAW_LOGE(TAG, "HTTP %d: %.200s", status, ctx.buf);
            snprintf(reply, reply_size, "[HTTP %d]", status);
        }
    } else {
        CLAW_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        snprintf(reply, reply_size, "[request failed: %s]",
                 esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    cJSON_free(body);
    claw_free(resp_buf);
    return ret;
}

int ai_engine_init(void)
{
    if (strlen(AI_API_KEY) == 0) {
        CLAW_LOGW(TAG, "no API key — set via: idf.py menuconfig");
    } else {
        CLAW_LOGI(TAG, "engine ready (model: %s)", AI_MODEL);
    }
    return CLAW_OK;
}

#else /* non-ESP-IDF platforms */

int ai_chat(const char *user_msg, char *reply, size_t reply_size)
{
    (void)user_msg;
    snprintf(reply, reply_size, "[AI not available on this platform]");
    return CLAW_ERROR;
}

int ai_engine_init(void)
{
    CLAW_LOGI(TAG, "engine initialized (inference backend pending)");
    return CLAW_OK;
}

#endif
