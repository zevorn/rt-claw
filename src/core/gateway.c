/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 */

#include "claw_os.h"
#include "gateway.h"

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
            CLAW_LOGD(TAG, "msg type=%d src=%d dst=%d len=%d",
                      msg.type, msg.src_channel, msg.dst_channel, msg.len);
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

    claw_thread_t t = claw_thread_create("gateway", gateway_thread_entry,
                                          NULL, CLAW_GW_THREAD_STACK,
                                          CLAW_GW_THREAD_PRIO);
    if (!t) {
        CLAW_LOGE(TAG, "thread create failed");
        return CLAW_ERROR;
    }

    CLAW_LOGI(TAG, "initialized");
    return CLAW_OK;
}

int gateway_send(struct gateway_msg *msg)
{
    return claw_mq_send(gw_mq, msg, sizeof(*msg), CLAW_NO_WAIT);
}
