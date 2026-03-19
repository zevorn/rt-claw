/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Gateway — message routing with pipeline processing and service registry.
 * OOP: private context struct embedding struct claw_service.
 */

#include "osal/claw_os.h"
#include "claw/core/gateway.h"
#include "claw/core/claw_service.h"

#include <string.h>

#define TAG "gateway"

/*
 * Gateway context — all state lives here, no file-scope globals.
 */
struct gateway_ctx {
    struct claw_service       base;

    struct gw_handler         handlers[GW_MAX_HANDLERS];
    int                       handler_count;

    struct gw_service_entry   services[GW_MAX_SERVICES];
    int                       service_count;

    struct gateway_stats      stats;

    struct claw_mq           *mq;
    struct claw_thread       *thread;
};

/* Forward declaration — defined at file end with designated initializer */
static struct gateway_ctx s_gw;

/* --- Pipeline handlers --- */

int gateway_register_handler(const char *name, gw_handler_fn process)
{
    struct gateway_ctx *ctx = &s_gw;

    if (ctx->handler_count >= GW_MAX_HANDLERS) {
        CLAW_LOGE(TAG, "handler registry full");
        return CLAW_ERROR;
    }
    if (!name || !process) {
        return CLAW_ERROR;
    }

    ctx->handlers[ctx->handler_count].name = name;
    ctx->handlers[ctx->handler_count].process = process;
    ctx->handler_count++;

    CLAW_LOGI(TAG, "handler registered: %s", name);
    return CLAW_OK;
}

/* --- Service consumers --- */

int gateway_register_service(const char *name, uint8_t type_mask,
                             struct claw_mq *inbox)
{
    struct gateway_ctx *ctx = &s_gw;

    if (ctx->service_count >= GW_MAX_SERVICES) {
        CLAW_LOGE(TAG, "service registry full");
        return CLAW_ERROR;
    }
    if (!name || !inbox) {
        return CLAW_ERROR;
    }

    ctx->services[ctx->service_count].name = name;
    ctx->services[ctx->service_count].type_mask = type_mask;
    ctx->services[ctx->service_count].inbox = inbox;
    ctx->service_count++;

    CLAW_LOGI(TAG, "service registered: %s (mask=0x%02x)", name, type_mask);
    return CLAW_OK;
}

/* --- Message dispatch --- */

static void dispatch_msg(struct gateway_ctx *ctx, struct gateway_msg *msg)
{
    ctx->stats.total++;
    if (msg->type < GW_MSG_TYPE_MAX) {
        ctx->stats.per_type[msg->type]++;
    }

    /* Polymorphic dispatch: if message has ops, use them */
    if (msg->ops && msg->ops->dispatch) {
        int rc = msg->ops->dispatch(msg);
        if (rc > 0) {
            ctx->stats.filtered++;
        }
        if (msg->ops && msg->ops->destroy) {
            msg->ops->destroy(msg);
        }
        return;
    }

    /* Pipeline: run handlers in registration order */
    for (int i = 0; i < ctx->handler_count; i++) {
        int rc = ctx->handlers[i].process(msg);
        if (rc > 0) {
            ctx->stats.filtered++;
            CLAW_LOGD(TAG, "msg type=%d consumed by %s",
                      msg->type, ctx->handlers[i].name);
            return;
        }
        if (rc < 0) {
            CLAW_LOGW(TAG, "handler %s error %d on type=%d",
                      ctx->handlers[i].name, rc, msg->type);
            return;
        }
    }

    /* Service dispatch: deliver to all matching consumers */
    uint8_t bit = (uint8_t)(1 << msg->type);
    int delivered = 0;

    for (int i = 0; i < ctx->service_count; i++) {
        if (ctx->services[i].type_mask & bit) {
            if (claw_mq_send(ctx->services[i].inbox, msg, sizeof(*msg),
                             CLAW_NO_WAIT) == CLAW_OK) {
                delivered++;
            } else {
                ctx->stats.dropped++;
                CLAW_LOGW(TAG, "drop msg type=%d -> %s (queue full)",
                          msg->type, ctx->services[i].name);
            }
        }
    }

    if (delivered == 0) {
        ctx->stats.no_consumer++;
        CLAW_LOGD(TAG, "msg type=%d len=%d (no consumer)", msg->type,
                  msg->len);
    }
}

/* --- Gateway thread --- */

static void gateway_thread_entry(void *param)
{
    struct gateway_ctx *ctx = (struct gateway_ctx *)param;
    struct gateway_msg msg;

    CLAW_LOGI(TAG, "started");

    while (!claw_thread_should_exit()) {
        if (claw_mq_recv(ctx->mq, &msg, sizeof(msg), 1000) == CLAW_OK) {
            dispatch_msg(ctx, &msg);
        }
    }
}

/* --- OOP lifecycle ops --- */

static claw_err_t gateway_svc_init(struct claw_service *svc)
{
    struct gateway_ctx *ctx = container_of(svc, struct gateway_ctx, base);

    memset(ctx->handlers, 0, sizeof(ctx->handlers));
    ctx->handler_count = 0;
    memset(ctx->services, 0, sizeof(ctx->services));
    memset(&ctx->stats, 0, sizeof(ctx->stats));
    ctx->service_count = 0;

    ctx->mq = claw_mq_create("gw_mq", sizeof(struct gateway_msg),
                              CLAW_GW_MSG_POOL_SIZE);
    if (!ctx->mq) {
        CLAW_LOGE(TAG, "mq create failed");
        return CLAW_ERR_NOMEM;
    }

    ctx->thread = claw_thread_create("gateway",
                                     gateway_thread_entry,
                                     ctx,
                                     CLAW_GW_THREAD_STACK,
                                     CLAW_GW_THREAD_PRIO);
    if (!ctx->thread) {
        CLAW_LOGE(TAG, "thread create failed");
        claw_mq_delete(ctx->mq);
        ctx->mq = NULL;
        return CLAW_ERR_NOMEM;
    }

    CLAW_LOGI(TAG, "initialized");
    return CLAW_OK;
}

static void gateway_svc_stop(struct claw_service *svc)
{
    struct gateway_ctx *ctx = container_of(svc, struct gateway_ctx, base);

    if (ctx->thread) {
        claw_thread_delete(ctx->thread);
        ctx->thread = NULL;
    }
    if (ctx->mq) {
        claw_mq_delete(ctx->mq);
        ctx->mq = NULL;
    }
}

/* --- Public API (delegates to context) --- */

int gateway_init(void)
{
    return gateway_svc_init(&s_gw.base) == CLAW_OK ? CLAW_OK : CLAW_ERROR;
}

void gateway_stop(void)
{
    gateway_svc_stop(&s_gw.base);
}

int gateway_send(struct gateway_msg *msg)
{
    return claw_mq_send(s_gw.mq, msg, sizeof(*msg), CLAW_NO_WAIT);
}

void gateway_get_stats(struct gateway_stats *out)
{
    if (out) {
        memcpy(out, &s_gw.stats, sizeof(s_gw.stats));
    }
}

/* --- OOP service registration --- */

static const struct claw_service_ops gateway_svc_ops = {
    .init  = gateway_svc_init,
    .start = NULL,
    .stop  = gateway_svc_stop,
};

static struct gateway_ctx s_gw = {
    .base = {
        .name  = "gateway",
        .ops   = &gateway_svc_ops,
        .deps  = NULL,
        .state = CLAW_SVC_CREATED,
    },
};

CLAW_SERVICE_REGISTER(gateway, &s_gw.base);
