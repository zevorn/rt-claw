/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Message gateway - central event bus with handler dispatch.
 */

#ifndef CLAW_CORE_GATEWAY_H
#define CLAW_CORE_GATEWAY_H

#include "claw_os.h"
#include "claw_config.h"

enum gateway_msg_type {
    GW_MSG_DATA = 0,
    GW_MSG_CMD,
    GW_MSG_EVENT,
    GW_MSG_SWARM,
    GW_MSG_TYPE_MAX,
};

struct gateway_msg {
    enum gateway_msg_type type;
    uint16_t len;
    uint8_t  payload[CLAW_GW_MSG_MAX_LEN];
};

/*
 * Message handler callback.
 * Called by the gateway thread for each matching message.
 * Must not block — heavy work should be deferred to another thread.
 */
typedef void (*gateway_handler_t)(const struct gateway_msg *msg, void *arg);

int gateway_init(void);
int gateway_send(struct gateway_msg *msg);

/*
 * Register a handler for a specific message type.
 * Multiple handlers per type are supported (up to CLAW_GW_MAX_HANDLERS).
 * Returns CLAW_OK or CLAW_ERROR if the handler table is full.
 */
int gateway_subscribe(enum gateway_msg_type type,
                      gateway_handler_t handler, void *arg);

#endif /* CLAW_CORE_GATEWAY_H */
