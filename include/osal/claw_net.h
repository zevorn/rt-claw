/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * OSAL network — platform-independent HTTP and socket abstraction.
 */

#ifndef CLAW_NET_H
#define CLAW_NET_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *key;
    const char *value;
} claw_net_header_t;

typedef int (*claw_net_body_cb_t)(const void *data, size_t len, void *user);

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

/*
 * Perform an HTTP POST request and stream response body chunks to cb.
 * Existing claw_net_post callers should keep using claw_net_post.
 */
int claw_net_post_stream(const char *url,
                         const claw_net_header_t *headers, int header_count,
                         const char *body, size_t body_len,
                         claw_net_body_cb_t cb, void *user,
                         size_t *resp_len);

/*
 * Perform an HTTP GET request.
 *
 * @param url          Full URL (http:// or https://)
 * @param headers      Custom request headers (may be NULL if count is 0)
 * @param header_count Number of entries in headers[]
 * @param resp         Buffer for response body (NUL-terminated on return)
 * @param resp_size    Size of resp buffer
 * @param resp_len     Actual response body length (may be NULL)
 *
 * @return HTTP status code (>0) on success,
 *         or negative CLAW_ERROR on transport failure.
 */
int claw_net_get(const char *url,
                 const claw_net_header_t *headers, int header_count,
                 char *resp, size_t resp_size, size_t *resp_len);

#ifdef __cplusplus
}
#endif

#endif /* CLAW_NET_H */
