/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * OSAL network — FreeRTOS implementation.
 *   ESP-IDF:     uses esp_http_client (full HTTPS)
 *   Standalone:  stub (network added in later phase)
 */

#include "osal/claw_os.h"
#include "osal/claw_net.h"

#include <string.h>

#ifdef CLAW_PLATFORM_ESP_IDF

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAG "net_http"
#define HTTP_TIMEOUT_MS  60000
#define MAX_CONCURRENT_TLS  2

/*
 * Counting semaphore to limit concurrent TLS connections.
 * ESP32-C3 has ~300KB heap; each TLS session uses ~30-40KB.
 * Allowing more than 2 simultaneous sessions prevents OOM.
 */
static SemaphoreHandle_t s_tls_sem;

static void tls_throttle_init(void)
{
    if (!s_tls_sem) {
        s_tls_sem = xSemaphoreCreateCounting(MAX_CONCURRENT_TLS,
                                              MAX_CONCURRENT_TLS);
    }
}

static void tls_throttle_acquire(void)
{
    tls_throttle_init();
    xSemaphoreTake(s_tls_sem, portMAX_DELAY);
}

static void tls_throttle_release(void)
{
    if (s_tls_sem) {
        xSemaphoreGive(s_tls_sem);
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

static esp_err_t on_http_stream_event(esp_http_client_event_t *evt)
{
    stream_ctx_t *ctx = (stream_ctx_t *)evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx) {
        if (evt->data_len > 0 && ctx->cb) {
            int rc = ctx->cb(evt->data, (size_t)evt->data_len, ctx->user);
            if (rc != CLAW_OK) {
                ctx->err = rc;
                return ESP_FAIL;
            }
        }
        ctx->len += (size_t)evt->data_len;
    }
    return ESP_OK;
}

int claw_net_post(const char *url,
                  const claw_net_header_t *headers, int header_count,
                  const char *body, size_t body_len,
                  char *resp, size_t resp_size, size_t *resp_len)
{
    tls_throttle_acquire();

    resp_ctx_t ctx = { .buf = resp, .size = resp_size, .len = 0 };
    resp[0] = '\0';

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
        tls_throttle_release();
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
    tls_throttle_release();

    if (err != ESP_OK) {
        CLAW_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
        return CLAW_ERROR;
    }

    if (resp_len) {
        *resp_len = ctx.len;
    }
    return status;
}

int claw_net_post_stream(const char *url,
                         const claw_net_header_t *headers, int header_count,
                         const char *body, size_t body_len,
                         claw_net_body_cb_t cb, void *user,
                         size_t *resp_len)
{
    tls_throttle_acquire();

    stream_ctx_t ctx = { .cb = cb, .user = user, .len = 0, .err = CLAW_OK };

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = on_http_stream_event,
        .user_data = &ctx,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        tls_throttle_release();
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
    tls_throttle_release();

    if (resp_len) {
        *resp_len = ctx.len;
    }
    if (err != ESP_OK) {
        if (ctx.err != CLAW_OK) {
            CLAW_LOGE(TAG, "HTTP POST stream callback failed: %d", ctx.err);
        } else {
            CLAW_LOGE(TAG, "HTTP POST stream failed: %s", esp_err_to_name(err));
        }
        return CLAW_ERROR;
    }
    return status;
}

int claw_net_get(const char *url,
                 const claw_net_header_t *headers, int header_count,
                 char *resp, size_t resp_size, size_t *resp_len)
{
    tls_throttle_acquire();

    resp_ctx_t ctx = { .buf = resp, .size = resp_size, .len = 0 };
    resp[0] = '\0';

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = on_http_event,
        .user_data = &ctx,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        tls_throttle_release();
        return CLAW_ERROR;
    }

    for (int i = 0; i < header_count; i++) {
        esp_http_client_set_header(client, headers[i].key,
                                   headers[i].value);
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = 0;
    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
    }
    esp_http_client_cleanup(client);
    tls_throttle_release();

    if (err != ESP_OK) {
        CLAW_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
        return CLAW_ERROR;
    }

    if (resp_len) {
        *resp_len = ctx.len;
    }
    return status;
}

#else /* Standalone FreeRTOS with FreeRTOS+TCP */

#include <stdio.h>
#include <stdlib.h>

#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_DNS.h"

#define TAG "net_http"
#define HTTP_TIMEOUT_MS 60000

typedef struct {
    char   *buf;
    size_t  size;
    size_t  len;
} resp_ctx_t;

static int write_body_cb(const void *data, size_t len, void *user)
{
    resp_ctx_t *ctx = (resp_ctx_t *)user;
    size_t avail = ctx->size - ctx->len - 1;
    size_t copy = (len < avail) ? len : avail;

    if (copy > 0) {
        memcpy(ctx->buf + ctx->len, data, copy);
        ctx->len += copy;
        ctx->buf[ctx->len] = '\0';
    }
    return CLAW_OK;
}

static int parse_url(const char *url, char *host, size_t host_sz,
                     int *port, char *path, size_t path_sz)
{
    const char *p = url;

    if (strncmp(p, "https://", 8) == 0) {
        *port = 443;
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        *port = 80;
        p += 7;
    } else {
        return -1;
    }

    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');
    size_t host_len;

    if (colon && (!slash || colon < slash)) {
        host_len = (size_t)(colon - p);
        *port = atoi(colon + 1);
    } else if (slash) {
        host_len = (size_t)(slash - p);
    } else {
        host_len = strlen(p);
    }

    if (host_len >= host_sz) {
        host_len = host_sz - 1;
    }
    memcpy(host, p, host_len);
    host[host_len] = '\0';

    if (slash) {
        snprintf(path, path_sz, "%s", slash);
    } else {
        snprintf(path, path_sz, "/");
    }

    return 0;
}

static int freertos_tcp_recv_response_cb(Socket_t sock,
                                         claw_net_body_cb_t cb,
                                         void *user,
                                         size_t *body_len_out,
                                         int *status_out)
{
    char tmp[512];
    char header[1024];
    size_t header_len = 0;
    int header_done = 0;
    int content_length = -1;
    int body_received = 0;

    *status_out = 0;
    *body_len_out = 0;
    header[0] = '\0';

    while (1) {
        BaseType_t n = FreeRTOS_recv(sock, tmp, sizeof(tmp) - 1, 0);
        if (n <= 0) {
            break;
        }
        tmp[n] = '\0';

        if (!header_done) {
            char *body_start;
            size_t avail = sizeof(header) - header_len - 1;
            size_t copy = ((size_t)n < avail) ? (size_t)n : avail;

            memcpy(header + header_len, tmp, copy);
            header_len += copy;
            header[header_len] = '\0';
            body_start = strstr(header, "\r\n\r\n");
            if (body_start) {
                size_t body_len;

                header_done = 1;
                body_start += 4;
                if (sscanf(header, "HTTP/%*d.%*d %d", status_out) != 1) {
                    *status_out = 0;
                }
                {
                    char *cl = strstr(header, "Content-Length:");
                    if (!cl) {
                        cl = strstr(header, "content-length:");
                    }
                    if (cl) {
                        content_length = atoi(cl + 15);
                    }
                }
                body_len = header_len - (size_t)(body_start - header);
                if (body_len > 0 && cb &&
                    cb(body_start, body_len, user) != CLAW_OK) {
                    return -1;
                }
                body_received = (int)body_len;
                *body_len_out += body_len;
            }
        } else {
            if (n > 0 && cb && cb(tmp, (size_t)n, user) != CLAW_OK) {
                return -1;
            }
            body_received += (int)n;
            *body_len_out += (size_t)n;
        }

        if (header_done && content_length >= 0 &&
            body_received >= content_length) {
            break;
        }
    }

    return header_done ? 0 : -1;
}

static int freertos_tcp_recv_response(Socket_t sock, char *buf,
                                      size_t buf_size,
                                      size_t *body_len_out,
                                      int *status_out)
{
    resp_ctx_t ctx = { .buf = buf, .size = buf_size, .len = 0 };

    buf[0] = '\0';
    if (freertos_tcp_recv_response_cb(sock, write_body_cb, &ctx,
                                      body_len_out, status_out) < 0) {
        return -1;
    }
    *body_len_out = ctx.len;
    return 0;
}

static Socket_t net_connect(const char *host, int port)
{
    uint32_t ip = FreeRTOS_gethostbyname(host);
    if (ip == 0U) {
        CLAW_LOGE(TAG, "DNS failed: %s", host);
        return FREERTOS_INVALID_SOCKET;
    }

    Socket_t sock = FreeRTOS_socket(FREERTOS_AF_INET,
                                     FREERTOS_SOCK_STREAM,
                                     FREERTOS_IPPROTO_TCP);
    if (sock == FREERTOS_INVALID_SOCKET) {
        CLAW_LOGE(TAG, "socket create failed");
        return FREERTOS_INVALID_SOCKET;
    }

    TickType_t timeout = pdMS_TO_TICKS(HTTP_TIMEOUT_MS);
    FreeRTOS_setsockopt(sock, 0, FREERTOS_SO_RCVTIMEO,
                        &timeout, sizeof(timeout));
    FreeRTOS_setsockopt(sock, 0, FREERTOS_SO_SNDTIMEO,
                        &timeout, sizeof(timeout));

    struct freertos_sockaddr addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = FREERTOS_AF_INET;
    addr.sin_port = FreeRTOS_htons((uint16_t)port);
    addr.sin_address.ulIP_IPv4 = ip;

    if (FreeRTOS_connect(sock, &addr, sizeof(addr)) != 0) {
        CLAW_LOGE(TAG, "connect failed: %s:%d", host, port);
        FreeRTOS_closesocket(sock);
        return FREERTOS_INVALID_SOCKET;
    }

    return sock;
}

int claw_net_post(const char *url,
                  const claw_net_header_t *headers, int header_count,
                  const char *body, size_t body_len,
                  char *resp, size_t resp_size, size_t *resp_len)
{
    char host[128];
    char path[256];
    int port;

    resp[0] = '\0';
    if (resp_len) {
        *resp_len = 0;
    }

    if (parse_url(url, host, sizeof(host),
                  &port, path, sizeof(path)) < 0) {
        CLAW_LOGE(TAG, "invalid URL: %s", url);
        return CLAW_ERROR;
    }

    if (port == 443) {
        CLAW_LOGW(TAG, "HTTPS not supported, trying plain HTTP on 443");
    }

    Socket_t sock = net_connect(host, port);
    if (sock == FREERTOS_INVALID_SOCKET) {
        return CLAW_ERROR;
    }

    char hdr[512];
    int hdr_len = snprintf(hdr, sizeof(hdr),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n",
        path, host, (int)body_len);

    for (int i = 0; i < header_count; i++) {
        hdr_len += snprintf(hdr + hdr_len,
                            (size_t)(sizeof(hdr) - (size_t)hdr_len),
                            "%s: %s\r\n",
                            headers[i].key, headers[i].value);
    }
    hdr_len += snprintf(hdr + hdr_len,
                        (size_t)(sizeof(hdr) - (size_t)hdr_len),
                        "\r\n");

    if (FreeRTOS_send(sock, hdr, (size_t)hdr_len, 0) < 0 ||
        FreeRTOS_send(sock, body, body_len, 0) < 0) {
        CLAW_LOGE(TAG, "send failed");
        FreeRTOS_closesocket(sock);
        return CLAW_ERROR;
    }

    int status = 0;
    size_t rlen = 0;
    int rc = freertos_tcp_recv_response(sock, resp, resp_size,
                                        &rlen, &status);
    FreeRTOS_closesocket(sock);

    if (rc < 0) {
        CLAW_LOGE(TAG, "response parse failed");
        return CLAW_ERROR;
    }

    if (resp_len) {
        *resp_len = rlen;
    }
    return status;
}

int claw_net_post_stream(const char *url,
                         const claw_net_header_t *headers, int header_count,
                         const char *body, size_t body_len,
                         claw_net_body_cb_t cb, void *user,
                         size_t *resp_len)
{
    char host[128];
    char path[256];
    int port;

    if (resp_len) {
        *resp_len = 0;
    }

    if (parse_url(url, host, sizeof(host),
                  &port, path, sizeof(path)) < 0) {
        CLAW_LOGE(TAG, "invalid URL: %s", url);
        return CLAW_ERROR;
    }

    if (port == 443) {
        CLAW_LOGW(TAG, "HTTPS not supported, trying plain HTTP on 443");
    }

    Socket_t sock = net_connect(host, port);
    if (sock == FREERTOS_INVALID_SOCKET) {
        return CLAW_ERROR;
    }

    char hdr[512];
    int hdr_len = snprintf(hdr, sizeof(hdr),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n",
        path, host, (int)body_len);

    for (int i = 0; i < header_count; i++) {
        hdr_len += snprintf(hdr + hdr_len,
                            (size_t)(sizeof(hdr) - (size_t)hdr_len),
                            "%s: %s\r\n",
                            headers[i].key, headers[i].value);
    }
    hdr_len += snprintf(hdr + hdr_len,
                        (size_t)(sizeof(hdr) - (size_t)hdr_len),
                        "\r\n");

    if (FreeRTOS_send(sock, hdr, (size_t)hdr_len, 0) < 0 ||
        FreeRTOS_send(sock, body, body_len, 0) < 0) {
        CLAW_LOGE(TAG, "send failed");
        FreeRTOS_closesocket(sock);
        return CLAW_ERROR;
    }

    int status = 0;
    size_t rlen = 0;
    int rc = freertos_tcp_recv_response_cb(sock, cb, user, &rlen, &status);
    FreeRTOS_closesocket(sock);

    if (resp_len) {
        *resp_len = rlen;
    }
    if (rc < 0) {
        CLAW_LOGE(TAG, "stream response parse failed");
        return CLAW_ERROR;
    }
    return status;
}

int claw_net_get(const char *url,
                 const claw_net_header_t *headers, int header_count,
                 char *resp, size_t resp_size, size_t *resp_len)
{
    char host[128];
    char path[256];
    int port;

    resp[0] = '\0';
    if (resp_len) {
        *resp_len = 0;
    }

    if (parse_url(url, host, sizeof(host),
                  &port, path, sizeof(path)) < 0) {
        CLAW_LOGE(TAG, "invalid URL: %s", url);
        return CLAW_ERROR;
    }

    if (port == 443) {
        CLAW_LOGW(TAG, "HTTPS not supported, trying plain HTTP on 443");
    }

    Socket_t sock = net_connect(host, port);
    if (sock == FREERTOS_INVALID_SOCKET) {
        return CLAW_ERROR;
    }

    char hdr[512];
    int hdr_len = snprintf(hdr, sizeof(hdr),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n",
        path, host);

    for (int i = 0; i < header_count; i++) {
        hdr_len += snprintf(hdr + hdr_len,
                            (size_t)(sizeof(hdr) - (size_t)hdr_len),
                            "%s: %s\r\n",
                            headers[i].key, headers[i].value);
    }
    hdr_len += snprintf(hdr + hdr_len,
                        (size_t)(sizeof(hdr) - (size_t)hdr_len),
                        "\r\n");

    if (FreeRTOS_send(sock, hdr, (size_t)hdr_len, 0) < 0) {
        CLAW_LOGE(TAG, "send failed");
        FreeRTOS_closesocket(sock);
        return CLAW_ERROR;
    }

    int status = 0;
    size_t rlen = 0;
    int rc = freertos_tcp_recv_response(sock, resp, resp_size,
                                        &rlen, &status);
    FreeRTOS_closesocket(sock);

    if (rc < 0) {
        CLAW_LOGE(TAG, "response parse failed");
        return CLAW_ERROR;
    }

    if (resp_len) {
        *resp_len = rlen;
    }
    return status;
}

#endif /* CLAW_PLATFORM_ESP_IDF */
