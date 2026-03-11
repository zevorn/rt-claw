/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 */

#include <rtthread.h>
#include "gateway.h"

static struct rt_messagequeue gw_mq;
static uint8_t gw_mq_pool[GATEWAY_MSG_POOL_SIZE * (sizeof(struct gateway_msg) + sizeof(void *))];
static struct rt_thread gw_thread;
static uint8_t gw_thread_stack[GATEWAY_THREAD_STACK];

static void gateway_thread_entry(void *param)
{
    struct gateway_msg msg;

    rt_kprintf("[gateway] started\n");

    while (1) {
        if (rt_mq_recv(&gw_mq, &msg, sizeof(msg), RT_WAITING_FOREVER) == RT_EOK) {
            rt_kprintf("[gateway] msg type=%d src=%d dst=%d len=%d\n",
                       msg.type, msg.src_channel, msg.dst_channel, msg.len);
        }
    }
}

int gateway_init(void)
{
    rt_err_t ret;

    ret = rt_mq_init(&gw_mq, "gw_mq",
                      gw_mq_pool, sizeof(struct gateway_msg),
                      sizeof(gw_mq_pool),
                      RT_IPC_FLAG_FIFO);
    if (ret != RT_EOK) {
        rt_kprintf("[gateway] mq init failed: %d\n", ret);
        return -1;
    }

    ret = rt_thread_init(&gw_thread, "gateway",
                          gateway_thread_entry, RT_NULL,
                          gw_thread_stack, sizeof(gw_thread_stack),
                          GATEWAY_THREAD_PRIO, 20);
    if (ret != RT_EOK) {
        rt_kprintf("[gateway] thread init failed: %d\n", ret);
        return -1;
    }

    rt_thread_startup(&gw_thread);
    rt_kprintf("[gateway] initialized\n");
    return 0;
}

int gateway_send(struct gateway_msg *msg)
{
    return rt_mq_send(&gw_mq, msg, sizeof(*msg));
}

INIT_APP_EXPORT(gateway_init);
