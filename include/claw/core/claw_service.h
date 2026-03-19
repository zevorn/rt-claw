/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Service framework — OOP base class with lifecycle state machine,
 * dependency declaration, and linker section auto-registration.
 */

#ifndef CLAW_CORE_CLAW_SERVICE_H
#define CLAW_CORE_CLAW_SERVICE_H

#include "claw/core/claw_errno.h"
#include "claw/core/claw_class.h"

/* ------------------------------------------------------------------ */
/* Service lifecycle states                                           */
/* ------------------------------------------------------------------ */

enum claw_service_state {
    CLAW_SVC_CREATED,
    CLAW_SVC_INITIALIZED,
    CLAW_SVC_RUNNING,
    CLAW_SVC_STOPPING,
    CLAW_SVC_STOPPED,
};

/* ------------------------------------------------------------------ */
/* Service ops vtable                                                 */
/* ------------------------------------------------------------------ */

struct claw_service;

struct claw_service_ops {
    claw_err_t (*init)(struct claw_service *svc);
    claw_err_t (*start)(struct claw_service *svc);    /* NULL = no-op */
    void       (*stop)(struct claw_service *svc);     /* NULL = no-op */
};

/* ------------------------------------------------------------------ */
/* Service base class                                                 */
/* ------------------------------------------------------------------ */

struct claw_service {
    const char                    *name;
    const struct claw_service_ops *ops;
    const char                   **deps;    /* NULL-terminated dep names */
    enum claw_service_state        state;
    claw_list_node_t               node;    /* registry linkage */
};

/* ------------------------------------------------------------------ */
/* Service core API                                                   */
/* ------------------------------------------------------------------ */

/*
 * Register a service into the runtime registry.
 * Validates ops->init is non-NULL.
 */
claw_err_t claw_service_register(struct claw_service *svc);

/*
 * Resolve dependencies and start all registered services in
 * topological order.  Returns CLAW_ERR_CYCLE on circular deps,
 * CLAW_ERR_DEPEND on missing deps.
 */
claw_err_t claw_service_start_all(void);

/*
 * Stop all running services in reverse order.
 */
void claw_service_stop_all(void);

/*
 * Query current state of a service.
 */
enum claw_service_state claw_service_get_state(const struct claw_service *svc);

/*
 * Collect services from the linker section into the runtime registry.
 * Called once from claw_init() before claw_service_start_all().
 */
claw_err_t claw_service_collect_from_section(void);

/*
 * Convenience macro to define a service that wraps legacy
 * init/start/stop functions (no context struct yet).
 * Usage:
 *   CLAW_DEFINE_SIMPLE_SERVICE(gateway, "gateway",
 *       gateway_init, NULL, gateway_stop, NULL);
 */
/*
 * Helper: wrap a legacy int (*fn)(void) into claw_err_t.
 * Returns CLAW_OK on success, CLAW_ERR_GENERIC on failure.
 */
static inline claw_err_t claw_svc_wrap_init(int (*fn)(void),
                                            struct claw_service *svc)
{
    (void)svc;
    if (!fn) {
        return CLAW_OK;
    }
    return fn() == CLAW_OK ? CLAW_OK : CLAW_ERR_GENERIC;
}

static inline claw_err_t claw_svc_wrap_start(int (*fn)(void),
                                             struct claw_service *svc)
{
    (void)svc;
    if (!fn) {
        return CLAW_OK;
    }
    return fn() == CLAW_OK ? CLAW_OK : CLAW_ERR_GENERIC;
}

static inline void claw_svc_wrap_stop(void (*fn)(void),
                                      struct claw_service *svc)
{
    (void)svc;
    if (fn) {
        fn();
    }
}

#define CLAW_DEFINE_SIMPLE_SERVICE(id, svc_name,                        \
                                   init_fn, start_fn, stop_fn, dep_arr) \
    static claw_err_t id##_svc_init(struct claw_service *svc)           \
    { return claw_svc_wrap_init(init_fn, svc); }                        \
    static claw_err_t id##_svc_start(struct claw_service *svc)          \
    { return claw_svc_wrap_start(start_fn, svc); }                      \
    static void id##_svc_stop(struct claw_service *svc)                 \
    { claw_svc_wrap_stop(stop_fn, svc); }                               \
    static const struct claw_service_ops id##_svc_ops = {               \
        .init  = id##_svc_init,                                         \
        .start = id##_svc_start,                                        \
        .stop  = id##_svc_stop,                                         \
    };                                                                  \
    static struct claw_service id##_svc = {                             \
        .name  = (svc_name),                                            \
        .ops   = &id##_svc_ops,                                         \
        .deps  = (dep_arr),                                             \
        .state = CLAW_SVC_CREATED,                                      \
    };                                                                  \
    CLAW_SERVICE_REGISTER(id, &id##_svc)

#endif /* CLAW_CORE_CLAW_SERVICE_H */
