/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "claw_os.h"
#include "core/gateway.h"

#define TAG "gateway"

typedef struct {
    enum gateway_msg_type type;
    gateway_handler_t     handler;
    void                 *arg;
} gw_sub_t;

static claw_mq_t gw_mq;
static gw_sub_t  s_subs[CLAW_GW_MAX_HANDLERS];
static int       s_sub_count;
static claw_mutex_t s_sub_lock;

static void dispatch(const struct gateway_msg *msg)
{
    claw_mutex_lock(s_sub_lock, CLAW_WAIT_FOREVER);

    for (int i = 0; i < s_sub_count; i++) {
        if (s_subs[i].type == msg->type) {
            s_subs[i].handler(msg, s_subs[i].arg);
        }
    }

    claw_mutex_unlock(s_sub_lock);
}

static void gateway_thread_entry(void *param)
{
    (void)param;
    struct gateway_msg msg;

    CLAW_LOGI(TAG, "started");

    while (1) {
        if (claw_mq_recv(gw_mq, &msg, sizeof(msg),
                          CLAW_WAIT_FOREVER) == CLAW_OK) {
            CLAW_LOGD(TAG, "msg type=%d len=%d", msg.type, msg.len);
            dispatch(&msg);
        }
    }
}

int gateway_init(void)
{
    s_sub_lock = claw_mutex_create("gw_sub");
    if (!s_sub_lock) {
        CLAW_LOGE(TAG, "mutex create failed");
        return CLAW_ERROR;
    }

    s_sub_count = 0;

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
        return CLAW_ERROR;
    }

    CLAW_LOGI(TAG, "initialized");
    return CLAW_OK;
}

int gateway_send(struct gateway_msg *msg)
{
    return claw_mq_send(gw_mq, msg, sizeof(*msg), CLAW_NO_WAIT);
}

int gateway_subscribe(enum gateway_msg_type type,
                      gateway_handler_t handler, void *arg)
{
    if (!handler || type >= GW_MSG_TYPE_MAX) {
        return CLAW_ERROR;
    }

    claw_mutex_lock(s_sub_lock, CLAW_WAIT_FOREVER);

    if (s_sub_count >= CLAW_GW_MAX_HANDLERS) {
        claw_mutex_unlock(s_sub_lock);
        CLAW_LOGE(TAG, "handler table full");
        return CLAW_ERROR;
    }

    gw_sub_t *s = &s_subs[s_sub_count];
    s->type = type;
    s->handler = handler;
    s->arg = arg;
    s_sub_count++;

    claw_mutex_unlock(s_sub_lock);
    CLAW_LOGD(TAG, "subscribed type=%d, handlers=%d",
              type, s_sub_count);
    return CLAW_OK;
}
