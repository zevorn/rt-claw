/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Network tools — HTTP GET/POST for AI agent.
 */

#include "claw/tools/claw_tools.h"
#include "claw/claw_config.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "tool_net"

/* Truncate response body to fit LLM context window */
#define NET_RESP_MAX    16384
#define NET_TIMEOUT_SEC 30

#ifdef CLAW_PLATFORM_ESP_IDF

#include "esp_http_client.h"
#include "esp_crt_bundle.h"

typedef struct {
    char *buf;
    size_t size;
    size_t len;
} resp_buf_t;

static esp_err_t on_http_data(esp_http_client_event_t *evt)
{
    resp_buf_t *r = (resp_buf_t *)evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_DATA && r) {
        size_t avail = r->size - r->len - 1;
        size_t copy = ((size_t)evt->data_len < avail)
                      ? (size_t)evt->data_len : avail;
        if (copy > 0) {
            memcpy(r->buf + r->len, evt->data, copy);
            r->len += copy;
            r->buf[r->len] = '\0';
        }
    }
    return ESP_OK;
}

static int tool_http_request(const cJSON *params, cJSON *result)
{
    cJSON *j_url = cJSON_GetObjectItem(params, "url");
    if (!j_url || !cJSON_IsString(j_url)) {
        cJSON_AddStringToObject(result, "error", "missing 'url' parameter");
        return CLAW_OK;
    }

    const char *url = j_url->valuestring;
    const char *method = "GET";
    const char *body = NULL;
    const char *content_type = "application/json";

    cJSON *j_method = cJSON_GetObjectItem(params, "method");
    if (j_method && cJSON_IsString(j_method)) {
        method = j_method->valuestring;
    }

    cJSON *j_body = cJSON_GetObjectItem(params, "body");
    if (j_body && cJSON_IsString(j_body)) {
        body = j_body->valuestring;
    }

    cJSON *j_ct = cJSON_GetObjectItem(params, "content_type");
    if (j_ct && cJSON_IsString(j_ct)) {
        content_type = j_ct->valuestring;
    }

    char *resp_buf = claw_malloc(NET_RESP_MAX);
    if (!resp_buf) {
        cJSON_AddStringToObject(result, "error", "out of memory");
        return CLAW_OK;
    }

    resp_buf_t ctx = { .buf = resp_buf, .size = NET_RESP_MAX, .len = 0 };

    esp_http_client_config_t cfg = {
        .url = url,
        .method = (strcmp(method, "POST") == 0)
                  ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .timeout_ms = NET_TIMEOUT_SEC * 1000,
        .event_handler = on_http_data,
        .user_data = &ctx,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        claw_free(resp_buf);
        cJSON_AddStringToObject(result, "error", "HTTP client init failed");
        return CLAW_OK;
    }

    esp_http_client_set_header(client, "Content-Type", content_type);
    esp_http_client_set_header(client, "User-Agent", "rt-claw/0.1");

    if (body) {
        esp_http_client_set_post_field(client, body, strlen(body));
    }

    CLAW_LOGI(TAG, "%s %s", method, url);
    esp_err_t err = esp_http_client_perform(client);
    int status = 0;

    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
    }
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        claw_free(resp_buf);
        cJSON_AddStringToObject(result, "error", "HTTP request failed");
        return CLAW_OK;
    }

    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddNumberToObject(result, "status_code", status);
    cJSON_AddStringToObject(result, "body", resp_buf);
    if (ctx.len >= NET_RESP_MAX - 1) {
        cJSON_AddBoolToObject(result, "truncated", 1);
    }

    claw_free(resp_buf);
    return CLAW_OK;
}

#elif defined(CLAW_PLATFORM_RTTHREAD)

#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

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

    if (slash) {
        snprintf(path, path_sz, "%s", slash);
    } else {
        snprintf(path, path_sz, "/");
    }

    return 0;
}

static int http_recv_body(int sock, char *buf, size_t buf_sz, int *status_out)
{
    char tmp[512];
    size_t total_len = 0;
    int header_done = 0;
    int content_length = -1;
    int body_received = 0;

    *status_out = 0;
    buf[0] = '\0';

    while (1) {
        int n = recv(sock, tmp, sizeof(tmp) - 1, 0);
        if (n <= 0) {
            break;
        }
        tmp[n] = '\0';

        if (!header_done) {
            size_t avail = buf_sz - total_len - 1;
            size_t copy = ((size_t)n < avail) ? (size_t)n : avail;
            memcpy(buf + total_len, tmp, copy);
            total_len += copy;
            buf[total_len] = '\0';

            char *body_start = strstr(buf, "\r\n\r\n");
            if (body_start) {
                header_done = 1;
                body_start += 4;

                sscanf(buf, "HTTP/%*d.%*d %d", status_out);

                char *cl = strstr(buf, "Content-Length:");
                if (!cl) {
                    cl = strstr(buf, "content-length:");
                }
                if (cl) {
                    content_length = atoi(cl + 15);
                }

                size_t body_len = total_len - (body_start - buf);
                memmove(buf, body_start, body_len);
                total_len = body_len;
                buf[total_len] = '\0';
                body_received = (int)body_len;
            }
        } else {
            size_t avail = buf_sz - total_len - 1;
            size_t copy = ((size_t)n < avail) ? (size_t)n : avail;
            memcpy(buf + total_len, tmp, copy);
            total_len += copy;
            buf[total_len] = '\0';
            body_received += n;
        }

        if (header_done && content_length >= 0 &&
            body_received >= content_length) {
            break;
        }
    }

    return header_done ? 0 : -1;
}

static int tool_http_request(const cJSON *params, cJSON *result)
{
    cJSON *j_url = cJSON_GetObjectItem(params, "url");
    if (!j_url || !cJSON_IsString(j_url)) {
        cJSON_AddStringToObject(result, "error", "missing 'url' parameter");
        return CLAW_OK;
    }

    const char *url = j_url->valuestring;
    const char *method = "GET";
    const char *body = NULL;
    const char *content_type = "application/json";

    cJSON *j_method = cJSON_GetObjectItem(params, "method");
    if (j_method && cJSON_IsString(j_method)) {
        method = j_method->valuestring;
    }

    cJSON *j_body = cJSON_GetObjectItem(params, "body");
    if (j_body && cJSON_IsString(j_body)) {
        body = j_body->valuestring;
    }

    cJSON *j_ct = cJSON_GetObjectItem(params, "content_type");
    if (j_ct && cJSON_IsString(j_ct)) {
        content_type = j_ct->valuestring;
    }

    char host[128];
    char path[256];
    int port;

    if (parse_url(url, host, sizeof(host), &port, path, sizeof(path)) < 0) {
        cJSON_AddStringToObject(result, "error", "invalid URL");
        return CLAW_OK;
    }

    if (port == 443) {
        cJSON_AddStringToObject(result, "error",
                                "HTTPS not supported on this platform, "
                                "use HTTP URL instead");
        return CLAW_OK;
    }

    char *resp_buf = claw_malloc(NET_RESP_MAX);
    if (!resp_buf) {
        cJSON_AddStringToObject(result, "error", "out of memory");
        return CLAW_OK;
    }

    struct hostent *he = gethostbyname(host);
    if (!he) {
        claw_free(resp_buf);
        cJSON_AddStringToObject(result, "error", "DNS resolve failed");
        return CLAW_OK;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        claw_free(resp_buf);
        cJSON_AddStringToObject(result, "error", "socket create failed");
        return CLAW_OK;
    }

    struct timeval tv;
    tv.tv_sec = NET_TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    memcpy(&server.sin_addr, he->h_addr, he->h_length);

    CLAW_LOGI(TAG, "%s %s:%d%s", method, host, port, path);

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        close(sock);
        claw_free(resp_buf);
        cJSON_AddStringToObject(result, "error", "connect failed");
        return CLAW_OK;
    }

    /* Build raw HTTP request */
    size_t body_len = body ? strlen(body) : 0;
    char header[512];
    int hdr_len = snprintf(header, sizeof(header),
        "%s %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: %s\r\n"
        "User-Agent: rt-claw/0.1\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        method, path, host, content_type, (int)body_len);

    if (send(sock, header, hdr_len, 0) < 0) {
        close(sock);
        claw_free(resp_buf);
        cJSON_AddStringToObject(result, "error", "send header failed");
        return CLAW_OK;
    }

    if (body && body_len > 0) {
        if (send(sock, body, body_len, 0) < 0) {
            close(sock);
            claw_free(resp_buf);
            cJSON_AddStringToObject(result, "error", "send body failed");
            return CLAW_OK;
        }
    }

    int status = 0;
    int rc = http_recv_body(sock, resp_buf, NET_RESP_MAX, &status);
    close(sock);

    if (rc < 0) {
        claw_free(resp_buf);
        cJSON_AddStringToObject(result, "error", "HTTP response parse failed");
        return CLAW_OK;
    }

    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddNumberToObject(result, "status_code", status);
    cJSON_AddStringToObject(result, "body", resp_buf);
    if (strlen(resp_buf) >= NET_RESP_MAX - 1) {
        cJSON_AddBoolToObject(result, "truncated", 1);
    }

    claw_free(resp_buf);
    return CLAW_OK;
}

#else /* unknown platform */

static int tool_http_request(const cJSON *params, cJSON *result)
{
    (void)params;
    cJSON_AddStringToObject(result, "error",
                            "networking not supported on this platform");
    return CLAW_OK;
}

#endif /* platform-specific */

static const char schema_http_request[] =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
        "\"url\":{\"type\":\"string\","
            "\"description\":\"HTTP(S) URL to request\"},"
        "\"method\":{\"type\":\"string\","
            "\"enum\":[\"GET\",\"POST\"],"
            "\"description\":\"HTTP method (default GET)\"},"
        "\"body\":{\"type\":\"string\","
            "\"description\":\"Request body (for POST)\"},"
        "\"content_type\":{\"type\":\"string\","
            "\"description\":\"Content-Type header "
            "(default application/json)\"}"
    "},"
    "\"required\":[\"url\"]"
    "}";

void claw_tools_register_net(void)
{
#ifdef CLAW_PLATFORM_ESP_IDF
    static const char desc[] =
        "Make an HTTP or HTTPS request (GET or POST). Returns status code "
        "and response body. IMPORTANT: responses larger than 16KB are "
        "truncated (truncated=true in result). Prefer compact formats "
        "(e.g. wttr.in?format=3) over verbose JSON APIs to avoid "
        "truncation. Both HTTP and HTTPS URLs are supported.";
#else
    static const char desc[] =
        "Make an HTTP request (GET or POST). Returns status code and "
        "response body. IMPORTANT: responses larger than 16KB are "
        "truncated (truncated=true in result). Prefer compact formats "
        "to avoid truncation. Only plain HTTP is supported (no HTTPS).";
#endif

    claw_tool_register("http_request", desc,
                       schema_http_request, tool_http_request);
}
