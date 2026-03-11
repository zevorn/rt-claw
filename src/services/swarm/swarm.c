/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 */

#include <rtthread.h>
#include "swarm.h"

static struct swarm_node nodes[SWARM_MAX_NODES];
static int node_count = 0;
static struct rt_mutex swarm_lock;

int swarm_init(void)
{
    rt_mutex_init(&swarm_lock, "swarm", RT_IPC_FLAG_PRIO);
    rt_memset(nodes, 0, sizeof(nodes));
    node_count = 0;
    rt_kprintf("[swarm] initialized, max_nodes=%d\n", SWARM_MAX_NODES);
    return 0;
}

int swarm_node_count(void)
{
    return node_count;
}

void swarm_list_nodes(void)
{
    int i;

    rt_mutex_take(&swarm_lock, RT_WAITING_FOREVER);
    rt_kprintf("[swarm] nodes online: %d/%d\n", node_count, SWARM_MAX_NODES);
    for (i = 0; i < SWARM_MAX_NODES; i++) {
        if (nodes[i].state != SWARM_NODE_OFFLINE) {
            rt_kprintf("  node[%d] id=0x%08x state=%d\n",
                       i, nodes[i].id, nodes[i].state);
        }
    }
    rt_mutex_release(&swarm_lock);
}

INIT_APP_EXPORT(swarm_init);

/* FinSH command to list swarm nodes */
#ifdef RT_USING_FINSH
#include <finsh.h>
MSH_CMD_EXPORT_ALIAS(swarm_list_nodes, swarm_list, list swarm nodes);
#endif
