/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Driver framework — OOP base class with probe/remove lifecycle
 * and linker section auto-registration.
 */

#ifndef CLAW_CORE_CLAW_DRIVER_H
#define CLAW_CORE_CLAW_DRIVER_H

#include "claw/core/claw_errno.h"
#include "claw/core/claw_class.h"

/* ------------------------------------------------------------------ */
/* Driver states                                                      */
/* ------------------------------------------------------------------ */

enum claw_driver_state {
    CLAW_DRV_REGISTERED,
    CLAW_DRV_PROBED,
    CLAW_DRV_SUSPENDED,
    CLAW_DRV_REMOVED,
};

/* ------------------------------------------------------------------ */
/* Driver ops vtable                                                  */
/* ------------------------------------------------------------------ */

struct claw_driver;

struct claw_driver_ops {
    claw_err_t (*probe)(struct claw_driver *drv);
    void       (*remove)(struct claw_driver *drv);
    claw_err_t (*suspend)(struct claw_driver *drv);  /* NULL = no-op */
    claw_err_t (*resume)(struct claw_driver *drv);   /* NULL = no-op */
};

/* ------------------------------------------------------------------ */
/* Driver base class                                                  */
/* ------------------------------------------------------------------ */

struct claw_driver {
    const char                   *name;
    const struct claw_driver_ops *ops;
    enum claw_driver_state        state;
    claw_list_node_t              node;    /* registry linkage */
};

/* ------------------------------------------------------------------ */
/* Driver core API                                                    */
/* ------------------------------------------------------------------ */

claw_err_t claw_driver_register(struct claw_driver *drv);
claw_err_t claw_driver_probe_all(void);
void       claw_driver_remove_all(void);
claw_err_t claw_driver_collect_from_section(void);

#endif /* CLAW_CORE_CLAW_DRIVER_H */
