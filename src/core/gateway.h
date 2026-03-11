/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 *
 * Message gateway - central message routing and channel management.
 * Inspired by OpenClaw's Gateway architecture, adapted for RTOS.
 */

#ifndef __RT_CLAW_GATEWAY_H__
#define __RT_CLAW_GATEWAY_H__

#include <rtthread.h>

#define GATEWAY_MSG_POOL_SIZE   16
#define GATEWAY_MSG_MAX_LEN     256
#define GATEWAY_THREAD_STACK    4096
#define GATEWAY_THREAD_PRIO     15

/* Message types */
enum gateway_msg_type {
    GW_MSG_DATA = 0,
    GW_MSG_CMD,
    GW_MSG_EVENT,
    GW_MSG_SWARM,
};

/* Gateway message */
struct gateway_msg {
    enum gateway_msg_type type;
    uint16_t src_channel;
    uint16_t dst_channel;
    uint16_t len;
    uint8_t  payload[GATEWAY_MSG_MAX_LEN];
};

int gateway_init(void);
int gateway_send(struct gateway_msg *msg);

#endif /* __RT_CLAW_GATEWAY_H__ */
