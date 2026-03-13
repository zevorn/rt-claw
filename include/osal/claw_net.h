/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * OSAL network — platform-independent HTTP and socket abstraction.
 */

#ifndef CLAW_NET_H
#define CLAW_NET_H

#include <stddef.h>

/* BSD socket API — platform-neutral includes */
#ifdef CLAW_PLATFORM_ESP_IDF
#include "lwip/sockets.h"
#elif defined(CLAW_PLATFORM_RTTHREAD)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *key;
    const char *value;
} claw_net_header_t;

/*
 * Perform an HTTP POST request.
 *
 * @param url          Full URL (http:// or https://)
 * @param headers      Custom request headers (may be NULL if count is 0)
 * @param header_count Number of entries in headers[]
 * @param body         Request body
 * @param body_len     Body length in bytes
 * @param resp         Buffer for response body (NUL-terminated on return)
 * @param resp_size    Size of resp buffer
 * @param resp_len     Actual response body length (may be NULL)
 *
 * @return HTTP status code (>0) on success,
 *         or negative CLAW_ERROR on transport failure.
 */
int claw_net_post(const char *url,
                  const claw_net_header_t *headers, int header_count,
                  const char *body, size_t body_len,
                  char *resp, size_t resp_size, size_t *resp_len);

#ifdef __cplusplus
}
#endif

#endif /* CLAW_NET_H */
