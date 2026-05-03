/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Minimal Zephyr platform driver for rt-claw.
 * Provides at least one valid CLAW_DRIVER_REGISTER entry so the
 * service framework can verify driver collection from linker sections.
 */

#include "osal/claw_os.h"
#include "claw/core/driver.h"

static claw_err_t zephyr_platform_probe(struct claw_driver *drv)
{
    (void)drv;
    CLAW_LOGI("zephyr_drv", "Zephyr platform driver probed");
    return CLAW_OK;
}

static void zephyr_platform_remove(struct claw_driver *drv)
{
    (void)drv;
}

static const struct claw_driver_ops s_zephyr_drv_ops = {
    .probe  = zephyr_platform_probe,
    .remove = zephyr_platform_remove,
};

static struct claw_driver s_zephyr_drv = {
    .name = "zephyr_platform",
    .ops  = &s_zephyr_drv_ops,
};

CLAW_DRIVER_REGISTER(zephyr_platform, &s_zephyr_drv);
