/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * OSAL network — ESP-IDF implementation using esp_http_client.
 */

#include "osal/claw_net.h"
#include "osal/claw_os.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#include <string.h>

#define TAG "net_http"
#define HTTP_TIMEOUT_MS 60000

typedef struct {
    char   *buf;
    size_t  size;
    size_t  len;
} resp_ctx_t;

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

int claw_net_post(const char *url,
                  const claw_net_header_t *headers, int header_count,
                  const char *body, size_t body_len,
                  char *resp, size_t resp_size, size_t *resp_len)
{
    resp_ctx_t ctx = { .buf = resp, .size = resp_size, .len = 0 };
    resp[0] = '\0';

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = on_http_event,
        .user_data = &ctx,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,
        .buffer_size_tx = 4096,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return CLAW_ERROR;
    }

    for (int i = 0; i < header_count; i++) {
        esp_http_client_set_header(client, headers[i].key,
                                   headers[i].value);
    }
    esp_http_client_set_post_field(client, body, (int)body_len);

    esp_err_t err = esp_http_client_perform(client);
    int status = 0;
    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
    }
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        CLAW_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
        return CLAW_ERROR;
    }

    if (resp_len) {
        *resp_len = ctx.len;
    }
    return status;
}
