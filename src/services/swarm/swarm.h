/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 *
 * Swarm service - node discovery, heartbeat, and task distribution
 * for building a mesh of rt-claw embedded nodes.
 */

#ifndef __RT_CLAW_SWARM_H__
#define __RT_CLAW_SWARM_H__

#include <rtthread.h>

#define SWARM_MAX_NODES         32
#define SWARM_HEARTBEAT_MS      5000
#define SWARM_NODE_TIMEOUT_MS   15000

/* Node state */
enum swarm_node_state {
    SWARM_NODE_OFFLINE = 0,
    SWARM_NODE_DISCOVERING,
    SWARM_NODE_ONLINE,
};

/* Swarm node descriptor */
struct swarm_node {
    uint32_t id;
    enum swarm_node_state state;
    uint32_t last_seen;     /* tick of last heartbeat */
    uint32_t ip_addr;       /* IPv4 address */
    uint16_t port;
    uint8_t  capabilities;  /* bitmask of node capabilities */
};

int  swarm_init(void);
int  swarm_node_count(void);
void swarm_list_nodes(void);

#endif /* __RT_CLAW_SWARM_H__ */
