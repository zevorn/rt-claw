/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Zephyr platform board abstraction.
 */

#include "platform/board.h"
#include "osal/claw_os.h"

#ifdef CONFIG_NET_SOCKETS_SOCKOPT_TLS
#include <zephyr/net/tls_credentials.h>

#define TLS_SEC_TAG  1

/*
 * ISRG Root X1 (Let's Encrypt) — DER-encoded CA certificate.
 * Minimal bundle for HTTPS to common AI API endpoints.
 */
static const unsigned char ca_cert[] = {
#include "isrg_root_x1.inc"
};

static int register_tls_ca(void)
{
    int ret = tls_credential_add(TLS_SEC_TAG,
                                  TLS_CREDENTIAL_CA_CERTIFICATE,
                                  ca_cert, sizeof(ca_cert));
    if (ret < 0) {
        CLAW_LOGE("board", "TLS CA registration failed: %d", ret);
    }
    return ret;
}
#endif

void board_early_init(void)
{
#ifdef CONFIG_NET_SOCKETS_SOCKOPT_TLS
    register_tls_ca();
#endif
}

const shell_cmd_t *board_platform_commands(int *count)
{
    if (count) {
        *count = 0;
    }
    return NULL;
}
