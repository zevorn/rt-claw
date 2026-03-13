/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * OSAL network — RT-Thread implementation using BSD sockets.
 * HTTP only (no TLS). For HTTPS use an HTTP proxy with TLS termination.
 */

#include "osal/claw_net.h"
#include "osal/claw_os.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#define TAG "net_http"
#define HTTP_TIMEOUT_SEC 60

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

static int http_recv_response(int sock, char *buf, size_t buf_size,
                              size_t *body_len_out, int *status_out)
{
    char tmp[512];
    size_t total_len = 0;
    int header_done = 0;
    char *body_start = NULL;
    int content_length = -1;
    int body_received = 0;

    *status_out = 0;
    *body_len_out = 0;
    buf[0] = '\0';

    while (1) {
        int n = recv(sock, tmp, sizeof(tmp) - 1, 0);
        if (n <= 0) {
            break;
        }
        tmp[n] = '\0';

        if (!header_done) {
            size_t avail = buf_size - total_len - 1;
            size_t copy = ((size_t)n < avail) ? (size_t)n : avail;
            memcpy(buf + total_len, tmp, copy);
            total_len += copy;
            buf[total_len] = '\0';

            body_start = strstr(buf, "\r\n\r\n");
            if (body_start) {
                header_done = 1;
                body_start += 4;

                if (sscanf(buf, "HTTP/%*d.%*d %d", status_out) != 1) {
                    *status_out = 0;
                }

                char *cl = strstr(buf, "Content-Length:");
                if (!cl) {
                    cl = strstr(buf, "content-length:");
                }
                if (cl) {
                    content_length = atoi(cl + 15);
                }

                size_t bl = total_len - (body_start - buf);
                memmove(buf, body_start, bl);
                total_len = bl;
                buf[total_len] = '\0';
                body_received = (int)bl;
            }
        } else {
            size_t avail = buf_size - total_len - 1;
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

    *body_len_out = total_len;
    return header_done ? 0 : -1;
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

    struct hostent *he = gethostbyname(host);
    if (!he) {
        CLAW_LOGE(TAG, "DNS failed: %s", host);
        return CLAW_ERROR;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        CLAW_LOGE(TAG, "socket create failed");
        return CLAW_ERROR;
    }

    struct timeval tv = { .tv_sec = HTTP_TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    memcpy(&server.sin_addr, he->h_addr, he->h_length);

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        CLAW_LOGE(TAG, "connect failed: %s:%d", host, port);
        close(sock);
        return CLAW_ERROR;
    }

    /* Build HTTP request */
    char hdr[512];
    int hdr_len = snprintf(hdr, sizeof(hdr),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n",
        path, host, (int)body_len);

    for (int i = 0; i < header_count; i++) {
        hdr_len += snprintf(hdr + hdr_len, sizeof(hdr) - hdr_len,
                            "%s: %s\r\n",
                            headers[i].key, headers[i].value);
    }
    hdr_len += snprintf(hdr + hdr_len, sizeof(hdr) - hdr_len, "\r\n");

    if (send(sock, hdr, hdr_len, 0) < 0 ||
        send(sock, body, body_len, 0) < 0) {
        CLAW_LOGE(TAG, "send failed");
        close(sock);
        return CLAW_ERROR;
    }

    int status = 0;
    size_t rlen = 0;
    int rc = http_recv_response(sock, resp, resp_size, &rlen, &status);
    close(sock);

    if (rc < 0) {
        CLAW_LOGE(TAG, "response parse failed");
        return CLAW_ERROR;
    }

    if (resp_len) {
        *resp_len = rlen;
    }
    return status;
}
