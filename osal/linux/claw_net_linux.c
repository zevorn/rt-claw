/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * OSAL network — Linux libcurl implementation (HTTP/HTTPS).
 */

#include "osal/claw_os.h"
#include "osal/claw_net.h"

#include <string.h>
#include <curl/curl.h>

#define TAG "net_http"
#define HTTP_TIMEOUT_S  60

static int s_curl_initialized;

static void ensure_curl_init(void)
{
    if (!s_curl_initialized) {
        curl_global_init(CURL_GLOBAL_ALL);
        s_curl_initialized = 1;
    }
}

typedef struct {
    char   *buf;
    size_t  size;
    size_t  len;
} resp_ctx_t;

typedef struct {
    claw_net_body_cb_t cb;
    void              *user;
    size_t             len;
    int                err;
} stream_ctx_t;

static size_t write_cb(void *data, size_t size, size_t nmemb, void *userp)
{
    resp_ctx_t *ctx = (resp_ctx_t *)userp;
    size_t total = size * nmemb;
    size_t avail = ctx->size - ctx->len - 1;
    size_t copy = (total < avail) ? total : avail;

    if (copy > 0) {
        memcpy(ctx->buf + ctx->len, data, copy);
        ctx->len += copy;
        ctx->buf[ctx->len] = '\0';
    }
    return total;
}

static size_t stream_write_cb(void *data, size_t size, size_t nmemb,
                              void *userp)
{
    stream_ctx_t *ctx = (stream_ctx_t *)userp;
    size_t total = size * nmemb;

    if (total > 0 && ctx->cb) {
        int rc = ctx->cb(data, total, ctx->user);
        if (rc != CLAW_OK) {
            ctx->err = rc;
            return 0;
        }
    }
    ctx->len += total;
    return total;
}

int claw_net_post(const char *url,
                  const claw_net_header_t *headers, int header_count,
                  const char *body, size_t body_len,
                  char *resp, size_t resp_size, size_t *resp_len)
{
    ensure_curl_init();

    resp_ctx_t ctx = { .buf = resp, .size = resp_size, .len = 0 };
    resp[0] = '\0';

    CURL *curl = curl_easy_init();
    if (!curl) {
        CLAW_LOGE(TAG, "curl_easy_init failed");
        return CLAW_ERROR;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)HTTP_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    struct curl_slist *hdr_list = NULL;
    for (int i = 0; i < header_count; i++) {
        char hdr[512];
        snprintf(hdr, sizeof(hdr), "%s: %s",
                 headers[i].key, headers[i].value);
        hdr_list = curl_slist_append(hdr_list, hdr);
    }
    if (hdr_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr_list);
    }

    CURLcode res = curl_easy_perform(curl);
    long status = 0;

    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    } else {
        CLAW_LOGE(TAG, "HTTP POST failed: %s", curl_easy_strerror(res));
    }

    curl_slist_free_all(hdr_list);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return CLAW_ERROR;
    }

    if (resp_len) {
        *resp_len = ctx.len;
    }
    return (int)status;
}

int claw_net_post_stream(const char *url,
                         const claw_net_header_t *headers, int header_count,
                         const char *body, size_t body_len,
                         claw_net_body_cb_t cb, void *user,
                         size_t *resp_len)
{
    ensure_curl_init();

    stream_ctx_t ctx = { .cb = cb, .user = user, .len = 0, .err = CLAW_OK };

    CURL *curl = curl_easy_init();
    if (!curl) {
        CLAW_LOGE(TAG, "curl_easy_init failed");
        return CLAW_ERROR;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)HTTP_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    struct curl_slist *hdr_list = NULL;
    for (int i = 0; i < header_count; i++) {
        char hdr[512];
        snprintf(hdr, sizeof(hdr), "%s: %s",
                 headers[i].key, headers[i].value);
        hdr_list = curl_slist_append(hdr_list, hdr);
    }
    if (hdr_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr_list);
    }

    CURLcode res = curl_easy_perform(curl);
    long status = 0;

    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    } else if (ctx.err != CLAW_OK) {
        CLAW_LOGE(TAG, "HTTP POST stream callback failed: %d", ctx.err);
    } else {
        CLAW_LOGE(TAG, "HTTP POST stream failed: %s", curl_easy_strerror(res));
    }

    curl_slist_free_all(hdr_list);
    curl_easy_cleanup(curl);

    if (resp_len) {
        *resp_len = ctx.len;
    }
    if (res != CURLE_OK) {
        return CLAW_ERROR;
    }
    return (int)status;
}

int claw_net_get(const char *url,
                 const claw_net_header_t *headers, int header_count,
                 char *resp, size_t resp_size, size_t *resp_len)
{
    ensure_curl_init();

    resp_ctx_t ctx = { .buf = resp, .size = resp_size, .len = 0 };
    resp[0] = '\0';

    CURL *curl = curl_easy_init();
    if (!curl) {
        CLAW_LOGE(TAG, "curl_easy_init failed");
        return CLAW_ERROR;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)HTTP_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    struct curl_slist *hdr_list = NULL;
    for (int i = 0; i < header_count; i++) {
        char hdr[512];
        snprintf(hdr, sizeof(hdr), "%s: %s",
                 headers[i].key, headers[i].value);
        hdr_list = curl_slist_append(hdr_list, hdr);
    }
    if (hdr_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr_list);
    }

    CURLcode res = curl_easy_perform(curl);
    long status = 0;

    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    } else {
        CLAW_LOGE(TAG, "HTTP GET failed: %s", curl_easy_strerror(res));
    }

    curl_slist_free_all(hdr_list);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return CLAW_ERROR;
    }

    if (resp_len) {
        *resp_len = ctx.len;
    }
    return (int)status;
}
