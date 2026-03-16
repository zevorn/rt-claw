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

/* --- Pipeline handlers --- */

static struct gw_handler s_handlers[GW_MAX_HANDLERS];
static int s_handler_count;

int gateway_register_handler(const char *name, gw_handler_fn process)
{
    if (s_handler_count >= GW_MAX_HANDLERS) {
        CLAW_LOGE(TAG, "handler registry full");
        return CLAW_ERROR;
    }
    if (!name || !process) {
        return CLAW_ERROR;
    }

    s_handlers[s_handler_count].name = name;
    s_handlers[s_handler_count].process = process;
    s_handler_count++;

    CLAW_LOGI(TAG, "handler registered: %s", name);
    return CLAW_OK;
}

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

/* --- Statistics --- */

static struct gateway_stats s_stats;

/* --- Message dispatch --- */

static void dispatch_msg(struct gateway_msg *msg)
{
    s_stats.total++;
    if (msg->type < GW_MSG_TYPE_MAX) {
        s_stats.per_type[msg->type]++;
    }

    /* Pipeline: run handlers in registration order */
    for (int i = 0; i < s_handler_count; i++) {
        int rc = s_handlers[i].process(msg);
        if (rc > 0) {
            s_stats.filtered++;
            CLAW_LOGD(TAG, "msg type=%d consumed by %s",
                      msg->type, s_handlers[i].name);
            return;
        }
        if (rc < 0) {
            CLAW_LOGW(TAG, "handler %s error %d on type=%d",
                      s_handlers[i].name, rc, msg->type);
            return;
        }
    }

    /* Service dispatch: deliver to all matching consumers */
    uint8_t bit = (uint8_t)(1 << msg->type);
    int delivered = 0;

    for (int i = 0; i < s_service_count; i++) {
        if (s_services[i].type_mask & bit) {
            if (claw_mq_send(s_services[i].inbox, msg, sizeof(*msg),
                             CLAW_NO_WAIT) == CLAW_OK) {
                delivered++;
            } else {
                s_stats.dropped++;
                CLAW_LOGW(TAG, "drop msg type=%d -> %s (queue full)",
                          msg->type, s_services[i].name);
            }
        }
    }

    if (delivered == 0) {
        s_stats.no_consumer++;
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
    memset(s_handlers, 0, sizeof(s_handlers));
    s_handler_count = 0;
    memset(s_services, 0, sizeof(s_services));
    memset(&s_stats, 0, sizeof(s_stats));
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

void gateway_get_stats(struct gateway_stats *out)
{
    if (out) {
        memcpy(out, &s_stats, sizeof(s_stats));
    }
}
