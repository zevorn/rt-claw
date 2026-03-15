/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Gateway — message routing with pipeline processing and service registry.
 */

#ifndef CLAW_CORE_GATEWAY_H
#define CLAW_CORE_GATEWAY_H

#include "osal/claw_os.h"
#include "claw_config.h"

enum gateway_msg_type {
    GW_MSG_DATA = 0,
    GW_MSG_CMD,
    GW_MSG_EVENT,
    GW_MSG_SWARM,
    GW_MSG_AI_REQ,
    GW_MSG_TYPE_MAX,
};

struct gateway_msg {
    enum gateway_msg_type type;
    uint16_t len;
    uint8_t  payload[CLAW_GW_MSG_MAX_LEN];
};

/*
 * Pipeline handler — processes messages in chain order.
 * Return: 0 = pass to next handler, 1 = consumed, <0 = error
 */
typedef int (*gw_handler_fn)(struct gateway_msg *msg);

struct gw_handler {
    const char *name;
    gw_handler_fn process;
};

/*
 * Service entry — registers a service as message consumer.
 * Services receive messages whose type matches the type_mask bitmap.
 */
#define GW_MAX_SERVICES     8

struct gw_service_entry {
    const char *name;
    uint8_t type_mask;          /* (1 << GW_MSG_*) bitmap */
    claw_mq_t inbox;            /* service's own message queue */
};

int gateway_init(void);
int gateway_send(struct gateway_msg *msg);

/**
 * Register a service to receive messages matching type_mask.
 * @param name       Service name (for logging)
 * @param type_mask  Bitmask of gateway_msg_type to receive
 * @param inbox      Service's message queue (gateway delivers to it)
 */
int gateway_register_service(const char *name, uint8_t type_mask,
                             claw_mq_t inbox);

#endif /* CLAW_CORE_GATEWAY_H */
