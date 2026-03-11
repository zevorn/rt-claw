/*
 * Copyright (c) 2025, rt-claw Development Team
 * SPDX-License-Identifier: MIT
 */

#include <rtthread.h>
#include "ai_engine.h"

int ai_engine_init(void)
{
    rt_kprintf("[ai] engine initialized (inference backend pending)\n");
    return 0;
}

INIT_APP_EXPORT(ai_engine_init);
