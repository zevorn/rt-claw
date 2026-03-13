/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "osal/claw_os.h"
#include "claw/core/gateway.h"

#define TAG "gateway"

static claw_mq_t gw_mq;

static void gateway_thread_entry(void *param)
{
    (void)param;
    struct gateway_msg msg;

    CLAW_LOGI(TAG, "started");

    while (1) {
        if (claw_mq_recv(gw_mq, &msg, sizeof(msg),
                          CLAW_WAIT_FOREVER) == CLAW_OK) {
            /* TODO: route to target node */
            CLAW_LOGD(TAG, "msg type=%d len=%d (not routed)",
                      msg.type, msg.len);
        }
    }
}

int gateway_init(void)
{
    gw_mq = claw_mq_create("gw_mq", sizeof(struct gateway_msg),
                             CLAW_GW_MSG_POOL_SIZE);
    if (!gw_mq) {
        CLAW_LOGE(TAG, "mq create failed");
        return CLAW_ERROR;
    }

    claw_thread_t t = claw_thread_create("gateway",
                                          gateway_thread_entry,
                                          NULL,
                                          CLAW_GW_THREAD_STACK,
                                          CLAW_GW_THREAD_PRIO);
    if (!t) {
        CLAW_LOGE(TAG, "thread create failed");
        claw_mq_delete(gw_mq);
        gw_mq = NULL;
        return CLAW_ERROR;
    }

    CLAW_LOGI(TAG, "initialized");
    return CLAW_OK;
}

int gateway_send(struct gateway_msg *msg)
{
    return claw_mq_send(gw_mq, msg, sizeof(*msg), CLAW_NO_WAIT);
}
