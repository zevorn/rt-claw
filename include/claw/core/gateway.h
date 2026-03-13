/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Gateway — inter-node message routing skeleton.
 */

#ifndef CLAW_CORE_GATEWAY_H
#define CLAW_CORE_GATEWAY_H

#include "osal/claw_os.h"
#include "claw/claw_config.h"

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

int gateway_init(void);
int gateway_send(struct gateway_msg *msg);

#endif /* CLAW_CORE_GATEWAY_H */
