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

/*
 * Message ops vtable — enables polymorphic dispatch.
 * All fields are optional (NULL = use default behavior).
 */
struct gateway_msg;

struct gateway_msg_ops {
    int  (*dispatch)(struct gateway_msg *msg);
    void (*destroy)(struct gateway_msg *msg);
    int  (*serialize)(const struct gateway_msg *msg, char *buf, size_t len);
    void (*dump)(const struct gateway_msg *msg);
};

struct gateway_msg {
    const struct gateway_msg_ops *ops;  /* NULL = legacy dispatch */
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
    struct claw_mq *inbox;            /* service's own message queue */
};

#define GW_MAX_HANDLERS     8

/* Message statistics */
struct gateway_stats {
    uint32_t total;                         /* total dispatched */
    uint32_t per_type[GW_MSG_TYPE_MAX];     /* per message type */
    uint32_t dropped;                       /* queue-full drops */
    uint32_t no_consumer;                   /* msgs with no match */
    uint32_t filtered;                      /* consumed by pipeline */
};

int gateway_init(void);
void gateway_stop(void);
int gateway_send(struct gateway_msg *msg);
void gateway_get_stats(struct gateway_stats *out);

/**
 * Register a pipeline handler. Handlers run in registration order
 * before service dispatch. A handler returning 1 consumes the
 * message (stops the chain); returning 0 passes it to the next.
 */
int gateway_register_handler(const char *name, gw_handler_fn process);

/**
 * Register a service to receive messages matching type_mask.
 * @param name       Service name (for logging)
 * @param type_mask  Bitmask of gateway_msg_type to receive
 * @param inbox      Service's message queue (gateway delivers to it)
 */
int gateway_register_service(const char *name, uint8_t type_mask,
                             struct claw_mq *inbox);

#endif /* CLAW_CORE_GATEWAY_H */
