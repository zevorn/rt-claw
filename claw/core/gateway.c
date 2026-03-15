/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Gateway — message routing with pipeline processing and service registry.
 */

#include "osal/claw_os.h"
#include "claw/core/gateway.h"

#include <string.h>

#define TAG "gateway"

/* --- Service registry --- */

static struct gw_service_entry s_services[GW_MAX_SERVICES];
static int s_service_count;

int gateway_register_service(const char *name, uint8_t type_mask,
                             claw_mq_t inbox)
{
    if (s_service_count >= GW_MAX_SERVICES) {
        CLAW_LOGE(TAG, "service registry full");
        return CLAW_ERROR;
    }
    if (!name || !inbox) {
        return CLAW_ERROR;
    }

    struct gw_service_entry *e = &s_services[s_service_count];
    e->name = name;
    e->type_mask = type_mask;
    e->inbox = inbox;
    s_service_count++;

    CLAW_LOGI(TAG, "service registered: %s (mask=0x%02x)", name, type_mask);
    return CLAW_OK;
}

/* --- Message dispatch --- */

static void dispatch_msg(struct gateway_msg *msg)
{
    uint8_t bit = (uint8_t)(1 << msg->type);
    int delivered = 0;

    for (int i = 0; i < s_service_count; i++) {
        if (s_services[i].type_mask & bit) {
            if (claw_mq_send(s_services[i].inbox, msg, sizeof(*msg),
                             CLAW_NO_WAIT) == CLAW_OK) {
                delivered++;
            } else {
                CLAW_LOGW(TAG, "drop msg type=%d -> %s (queue full)",
                          msg->type, s_services[i].name);
            }
        }
    }

    if (delivered == 0) {
        CLAW_LOGD(TAG, "msg type=%d len=%d (no consumer)", msg->type,
                  msg->len);
    }
}

/* --- Gateway thread --- */

static claw_mq_t gw_mq;

static void gateway_thread_entry(void *param)
{
    (void)param;
    struct gateway_msg msg;

    CLAW_LOGI(TAG, "started");

    while (1) {
        if (claw_mq_recv(gw_mq, &msg, sizeof(msg),
                          CLAW_WAIT_FOREVER) == CLAW_OK) {
            dispatch_msg(&msg);
        }
    }
}

int gateway_init(void)
{
    memset(s_services, 0, sizeof(s_services));
    s_service_count = 0;

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
