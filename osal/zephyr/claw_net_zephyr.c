/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * OSAL network implementation for Zephyr RTOS.
 * Uses Zephyr HTTP Client API with BSD-like sockets.
 */

#include "osal/claw_net.h"
#include "osal/claw_os.h"

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>
#include <zephyr/logging/log.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(claw_net, LOG_LEVEL_INF);

#define HTTP_TIMEOUT_MS     30000
#define MAX_HEADERS         16
#define HDR_BUF_SIZE        512
#define RECV_BUF_SIZE       2048

/* ---------- URL parser ---------- */

struct parsed_url {
    int   is_https;
    char  host[128];
    char  port_str[8];
    int   port;
    char  path[512];
};

static int parse_url(const char *url, struct parsed_url *out)
{
    memset(out, 0, sizeof(*out));

    if (strncmp(url, "https://", 8) == 0) {
        out->is_https = 1;
        url += 8;
        out->port = 443;
    } else if (strncmp(url, "http://", 7) == 0) {
        out->is_https = 0;
        url += 7;
        out->port = 80;
    } else {
        return -1;
    }

    const char *slash = strchr(url, '/');
    const char *colon = strchr(url, ':');

    if (colon && (!slash || colon < slash)) {
        size_t hlen = (size_t)(colon - url);

        if (hlen >= sizeof(out->host)) {
            return -1;
        }
        memcpy(out->host, url, hlen);
        out->host[hlen] = '\0';
        out->port = atoi(colon + 1);
    } else if (slash) {
        size_t hlen = (size_t)(slash - url);

        if (hlen >= sizeof(out->host)) {
            return -1;
        }
        memcpy(out->host, url, hlen);
        out->host[hlen] = '\0';
    } else {
        size_t hlen = strlen(url);

        if (hlen >= sizeof(out->host)) {
            return -1;
        }
        strcpy(out->host, url);
    }

    snprintf(out->port_str, sizeof(out->port_str), "%d", out->port);

    if (slash) {
        size_t plen = strlen(slash);

        if (plen >= sizeof(out->path)) {
            return -1;
        }
        strcpy(out->path, slash);
    } else {
        strcpy(out->path, "/");
    }

    return 0;
}

/* ---------- response callback ---------- */

struct http_ctx {
    char   *resp_buf;
    size_t  resp_size;
    size_t  resp_len;
    int     http_status;
};

static int response_cb(struct http_response *rsp,
                        enum http_final_call final_data,
                        void *user_data)
{
    struct http_ctx *ctx = user_data;

    (void)final_data;

    ctx->http_status = rsp->http_status_code;

    if (rsp->body_frag_len > 0 && ctx->resp_buf && ctx->resp_size > 1) {
        size_t avail = 0;

        if (ctx->resp_len < ctx->resp_size - 1) {
            avail = ctx->resp_size - 1 - ctx->resp_len;
        }
        size_t to_copy = rsp->body_frag_len;

        if (to_copy > avail) {
            to_copy = avail;
        }
        if (to_copy > 0) {
            memcpy(ctx->resp_buf + ctx->resp_len,
                   rsp->body_frag_start, to_copy);
            ctx->resp_len += to_copy;
        }
    }

    return 0;
}

/* ---------- socket helpers ---------- */

#ifdef CONFIG_NET_SOCKETS_SOCKOPT_TLS
#include <zephyr/net/tls_credentials.h>
#define TLS_SEC_TAG  1
#endif

static int create_and_connect(const struct parsed_url *pu)
{
    struct zsock_addrinfo hints;
    struct zsock_addrinfo *res = NULL;
    int sock = -1;
    int ret;
    int proto;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    ret = zsock_getaddrinfo(pu->host, pu->port_str, &hints, &res);
    if (ret || !res) {
        LOG_ERR("DNS resolve failed for %s: %d", pu->host, ret);
        return -1;
    }

#ifdef CONFIG_NET_SOCKETS_SOCKOPT_TLS
    proto = pu->is_https ? IPPROTO_TLS_1_2 : IPPROTO_TCP;
#else
    if (pu->is_https) {
        LOG_ERR("HTTPS requested but TLS not configured");
        zsock_freeaddrinfo(res);
        return -1;
    }
    proto = IPPROTO_TCP;
#endif

    sock = zsock_socket(res->ai_family, SOCK_STREAM, proto);
    if (sock < 0) {
        LOG_ERR("Socket create failed: %d", errno);
        zsock_freeaddrinfo(res);
        return -1;
    }

#ifdef CONFIG_NET_SOCKETS_SOCKOPT_TLS
    if (pu->is_https) {
        sec_tag_t sec_tags[] = { TLS_SEC_TAG };

        ret = zsock_setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST,
                               sec_tags, sizeof(sec_tags));
        if (ret < 0) {
            LOG_ERR("TLS_SEC_TAG_LIST failed: %d", errno);
            zsock_close(sock);
            zsock_freeaddrinfo(res);
            return -1;
        }

        ret = zsock_setsockopt(sock, SOL_TLS, TLS_HOSTNAME,
                               pu->host, strlen(pu->host));
        if (ret < 0) {
            LOG_ERR("TLS_HOSTNAME failed: %d", errno);
            zsock_close(sock);
            zsock_freeaddrinfo(res);
            return -1;
        }
    }
#endif

    ret = zsock_connect(sock, res->ai_addr, res->ai_addrlen);
    zsock_freeaddrinfo(res);

    if (ret < 0) {
        LOG_ERR("Connect to %s:%d failed: %d",
                pu->host, pu->port, errno);
        zsock_close(sock);
        return -1;
    }

    return sock;
}

/* ---------- common request ---------- */

static int do_request(enum http_method method,
                      const struct parsed_url *pu,
                      const claw_net_header_t *headers, int header_count,
                      const char *body, size_t body_len,
                      char *resp, size_t resp_size, size_t *resp_len)
{
    int sock = create_and_connect(pu);

    if (sock < 0) {
        return CLAW_ERR_IO;
    }

    uint8_t recv_buf[RECV_BUF_SIZE];

    struct http_ctx ctx = {
        .resp_buf  = resp,
        .resp_size = resp_size,
        .resp_len  = 0,
        .http_status = 0,
    };

    /*
     * Build NULL-terminated header field array for Zephyr HTTP Client.
     * Each pair is: "Header-Name: value\r\n"
     */
    const char *hdr_strs[MAX_HEADERS + 1];
    char hdr_buf[MAX_HEADERS][HDR_BUF_SIZE];
    int hdr_count = 0;

    for (int i = 0;
         i < header_count && headers && hdr_count < MAX_HEADERS;
         i++) {
        snprintf(hdr_buf[hdr_count], sizeof(hdr_buf[0]),
                 "%s: %s\r\n", headers[i].key, headers[i].value);
        hdr_strs[hdr_count] = hdr_buf[hdr_count];
        hdr_count++;
    }
    hdr_strs[hdr_count] = NULL;

    struct http_request req;

    memset(&req, 0, sizeof(req));
    req.method       = method;
    req.url          = pu->path;
    req.host         = pu->host;
    req.port         = pu->port_str;
    req.protocol     = "HTTP/1.1";
    req.response     = response_cb;
    req.recv_buf     = recv_buf;
    req.recv_buf_len = sizeof(recv_buf);

    if (hdr_count > 0) {
        req.header_fields = hdr_strs;
    }

    if (body && body_len > 0) {
        req.payload     = body;
        req.payload_len = body_len;
    }

    int ret = http_client_req(sock, &req, HTTP_TIMEOUT_MS, &ctx);

    zsock_close(sock);

    if (ret < 0) {
        LOG_ERR("HTTP request failed: %d", ret);
        if (resp && resp_size > 0) {
            resp[0] = '\0';
        }
        return CLAW_ERR_IO;
    }

    if (resp && ctx.resp_len < resp_size) {
        resp[ctx.resp_len] = '\0';
    } else if (resp && resp_size > 0) {
        resp[resp_size - 1] = '\0';
    }

    if (resp_len) {
        *resp_len = ctx.resp_len;
    }

    return ctx.http_status;
}

/* ---------- public API ---------- */

int claw_net_post(const char *url,
                  const claw_net_header_t *headers, int header_count,
                  const char *body, size_t body_len,
                  char *resp, size_t resp_size, size_t *resp_len)
{
    struct parsed_url pu;

    if (parse_url(url, &pu) < 0) {
        return CLAW_ERR_INVALID;
    }
    return do_request(HTTP_POST, &pu, headers, header_count,
                      body, body_len, resp, resp_size, resp_len);
}

int claw_net_get(const char *url,
                 const claw_net_header_t *headers, int header_count,
                 char *resp, size_t resp_size, size_t *resp_len)
{
    struct parsed_url pu;

    if (parse_url(url, &pu) < 0) {
        return CLAW_ERR_INVALID;
    }
    return do_request(HTTP_GET, &pu, headers, header_count,
                      NULL, 0, resp, resp_size, resp_len);
}
