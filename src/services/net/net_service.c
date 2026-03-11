/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 */

#include <rtthread.h>
#include "net_service.h"

int net_service_init(void)
{
    rt_kprintf("[net] service initialized (lwIP pending configuration)\n");
    return 0;
}

INIT_APP_EXPORT(net_service_init);
