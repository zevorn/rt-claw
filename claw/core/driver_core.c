/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Driver core — registration, probe/remove lifecycle.
 */

#include "claw/core/claw_driver.h"
#include "osal/claw_os.h"

#define TAG "drv_core"

/*
 * Runtime registry
 */

static CLAW_LIST_HEAD(s_drivers);

/*
 * Registration
 */

claw_err_t claw_driver_register(struct claw_driver *drv)
{
    if (!drv || !drv->name) {
        return CLAW_ERR_INVALID;
    }

    CLAW_OPS_VALIDATE(drv->ops, probe);

    drv->state = CLAW_DRV_REGISTERED;
    claw_list_init(&drv->node);
    claw_list_add_tail(&drv->node, &s_drivers);

    CLAW_LOGD(TAG, "registered: %s", drv->name);
    return CLAW_OK;
}

/*
 * Section collection
 */

claw_err_t claw_driver_collect_from_section(void)
{
    const struct claw_driver **p;

    if (!__start_claw_drivers || !__stop_claw_drivers) {
        return CLAW_OK;
    }

    claw_for_each_registered(p, __start_claw_drivers,
                             __stop_claw_drivers) {
        claw_err_t err = claw_driver_register(
            (struct claw_driver *)*p);
        if (err != CLAW_OK) {
            CLAW_LOGE(TAG, "failed to register %s: %s",
                      (*p)->name, claw_strerror(err));
        }
    }

    return CLAW_OK;
}

/*
 * Lifecycle
 */

claw_err_t claw_driver_probe_all(void)
{
    claw_list_node_t *pos;

    claw_list_for_each(pos, &s_drivers) {
        struct claw_driver *drv = claw_list_entry(
            pos, struct claw_driver, node);

        if (drv->state != CLAW_DRV_REGISTERED) {
            continue;
        }

        CLAW_LOGI(TAG, "probe: %s", drv->name);
        claw_err_t err = drv->ops->probe(drv);
        if (err != CLAW_OK) {
            CLAW_LOGE(TAG, "%s probe failed: %s",
                      drv->name, claw_strerror(err));
            continue;
        }
        drv->state = CLAW_DRV_PROBED;
    }

    return CLAW_OK;
}

void claw_driver_remove_all(void)
{
    claw_list_node_t *pos;
    claw_list_node_t *tmp;

    claw_list_for_each_safe(pos, tmp, &s_drivers) {
        struct claw_driver *drv = claw_list_entry(
            pos, struct claw_driver, node);

        if (drv->state != CLAW_DRV_PROBED) {
            continue;
        }

        if (drv->ops->remove) {
            CLAW_LOGI(TAG, "remove: %s", drv->name);
            drv->ops->remove(drv);
        }
        drv->state = CLAW_DRV_REMOVED;
    }
}
