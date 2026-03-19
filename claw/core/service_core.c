/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Service core — registration, dependency resolution, lifecycle.
 */

#include "claw/core/claw_service.h"
#include "osal/claw_os.h"
#include <string.h>

#define TAG "svc_core"
#define MAX_SERVICES 16

/*
 * Runtime registry
 */

static CLAW_LIST_HEAD(s_registry);
static int s_count;

/*
 * Registration
 */

claw_err_t claw_service_register(struct claw_service *svc)
{
    if (!svc || !svc->name) {
        return CLAW_ERR_INVALID;
    }

    CLAW_OPS_VALIDATE(svc->ops, init);

    svc->state = CLAW_SVC_CREATED;
    claw_list_init(&svc->node);
    claw_list_add_tail(&svc->node, &s_registry);
    s_count++;

    CLAW_LOGD(TAG, "registered: %s", svc->name);
    return CLAW_OK;
}

/*
 * Section collection
 */

claw_err_t claw_service_collect_from_section(void)
{
    const struct claw_service **p;

    if (!__start_claw_services || !__stop_claw_services) {
        return CLAW_OK;
    }

    claw_for_each_registered(p, __start_claw_services,
                             __stop_claw_services) {
        claw_err_t err = claw_service_register(
            (struct claw_service *)*p);
        if (err != CLAW_OK) {
            CLAW_LOGE(TAG, "failed to register %s: %s",
                      (*p)->name, claw_strerror(err));
        }
    }

    return CLAW_OK;
}

/*
 * Dependency resolution (topological sort via Kahn's algorithm)
 */

static struct claw_service *find_by_name(const char *name)
{
    claw_list_node_t *pos;

    claw_list_for_each(pos, &s_registry) {
        struct claw_service *svc = claw_list_entry(
            pos, struct claw_service, node);
        if (strcmp(svc->name, name) == 0) {
            return svc;
        }
    }
    return NULL;
}

static int count_unmet_deps(struct claw_service *svc,
                            struct claw_service **sorted, int n_sorted)
{
    int unmet = 0;

    if (!svc->deps) {
        return 0;
    }

    for (const char **d = svc->deps; *d; d++) {
        int found = 0;
        for (int i = 0; i < n_sorted; i++) {
            if (strcmp(sorted[i]->name, *d) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            unmet++;
        }
    }
    return unmet;
}

static claw_err_t topo_sort(struct claw_service **sorted, int *out_count)
{
    struct claw_service *all[MAX_SERVICES];
    int total = 0;
    int placed = 0;
    claw_list_node_t *pos;

    claw_list_for_each(pos, &s_registry) {
        if (total >= MAX_SERVICES) {
            CLAW_LOGE(TAG, "too many services (max %d)", MAX_SERVICES);
            return CLAW_ERR_FULL;
        }
        all[total++] = claw_list_entry(pos, struct claw_service, node);
    }

    for (int i = 0; i < total; i++) {
        if (!all[i]->deps) {
            continue;
        }
        for (const char **d = all[i]->deps; *d; d++) {
            if (!find_by_name(*d)) {
                CLAW_LOGE(TAG, "%s: missing dep '%s'",
                          all[i]->name, *d);
                return CLAW_ERR_DEPEND;
            }
        }
    }

    while (placed < total) {
        int progress = 0;

        for (int i = 0; i < total; i++) {
            if (!all[i]) {
                continue;
            }
            if (count_unmet_deps(all[i], sorted, placed) == 0) {
                sorted[placed++] = all[i];
                all[i] = NULL;
                progress = 1;
            }
        }
        if (!progress) {
            CLAW_LOGE(TAG, "circular dependency detected");
            return CLAW_ERR_CYCLE;
        }
    }

    *out_count = placed;
    return CLAW_OK;
}

/*
 * Lifecycle
 */

claw_err_t claw_service_start_all(void)
{
    struct claw_service *sorted[MAX_SERVICES];
    int count = 0;
    claw_err_t err;

    err = topo_sort(sorted, &count);
    if (err != CLAW_OK) {
        return err;
    }

    for (int i = 0; i < count; i++) {
        struct claw_service *svc = sorted[i];

        CLAW_LOGI(TAG, "init: %s", svc->name);
        err = svc->ops->init(svc);
        if (err != CLAW_OK) {
            CLAW_LOGE(TAG, "%s init failed: %s",
                      svc->name, claw_strerror(err));
            svc->state = CLAW_SVC_STOPPED;
            continue;
        }
        svc->state = CLAW_SVC_INITIALIZED;
    }

    for (int i = 0; i < count; i++) {
        struct claw_service *svc = sorted[i];

        if (svc->state != CLAW_SVC_INITIALIZED) {
            continue;
        }
        if (!svc->ops->start) {
            svc->state = CLAW_SVC_RUNNING;
            continue;
        }

        CLAW_LOGI(TAG, "start: %s", svc->name);
        err = svc->ops->start(svc);
        if (err != CLAW_OK) {
            CLAW_LOGE(TAG, "%s start failed: %s",
                      svc->name, claw_strerror(err));
            svc->state = CLAW_SVC_STOPPED;
            continue;
        }
        svc->state = CLAW_SVC_RUNNING;
    }

    return CLAW_OK;
}

void claw_service_stop_all(void)
{
    struct claw_service *sorted[MAX_SERVICES];
    int count = 0;

    if (topo_sort(sorted, &count) != CLAW_OK) {
        return;
    }

    for (int i = count - 1; i >= 0; i--) {
        struct claw_service *svc = sorted[i];

        if (svc->state != CLAW_SVC_RUNNING &&
            svc->state != CLAW_SVC_INITIALIZED) {
            continue;
        }

        svc->state = CLAW_SVC_STOPPING;
        if (svc->ops->stop) {
            CLAW_LOGI(TAG, "stop: %s", svc->name);
            svc->ops->stop(svc);
        }
        svc->state = CLAW_SVC_STOPPED;
    }
}

enum claw_service_state claw_service_get_state(
    const struct claw_service *svc)
{
    if (!svc) {
        return CLAW_SVC_STOPPED;
    }
    return svc->state;
}
