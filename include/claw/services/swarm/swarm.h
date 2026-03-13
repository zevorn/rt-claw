/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Swarm service - node discovery, heartbeat, and task distribution.
 */

#ifndef CLAW_SERVICES_SWARM_H
#define CLAW_SERVICES_SWARM_H

#include "osal/claw_os.h"
#include "claw/claw_config.h"

enum swarm_node_state {
    SWARM_NODE_OFFLINE = 0,
    SWARM_NODE_DISCOVERING,
    SWARM_NODE_ONLINE,
};

struct swarm_node {
    uint32_t id;
    enum swarm_node_state state;
    uint32_t last_seen;
    uint32_t ip_addr;
    uint16_t port;
    uint8_t  capabilities;
    uint8_t  load;
    uint32_t uptime_s;
};

/* 16-byte heartbeat packet (packed) */
struct __attribute__((packed)) swarm_heartbeat {
    uint32_t magic;         /* 0x434C4157 "CLAW" */
    uint32_t node_id;
    uint32_t uptime_s;
    uint8_t  capabilities;
    uint8_t  load;
    uint16_t port;
};

#define SWARM_HEARTBEAT_MAGIC   0x434C4157  /* "CLAW" */

int  swarm_init(void);
int  swarm_start(void);
uint32_t swarm_self_id(void);
int  swarm_node_count(void);
void swarm_list_nodes(void);

#endif /* CLAW_SERVICES_SWARM_H */
