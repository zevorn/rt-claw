/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Tool core — registration and linker section collection.
 */

#include "claw/core/claw_tool.h"
#include "osal/claw_os.h"

#define TAG "tool_core"

static CLAW_LIST_HEAD(s_tools);

claw_err_t claw_tool_core_register(struct claw_tool *tool)
{
    if (!tool || !tool->name) {
        return CLAW_ERR_INVALID;
    }

    CLAW_OPS_VALIDATE(tool->ops, execute);

    claw_list_init(&tool->node);
    claw_list_add_tail(&tool->node, &s_tools);

    CLAW_LOGD(TAG, "registered: %s", tool->name);
    return CLAW_OK;
}

claw_err_t claw_tool_core_collect_from_section(void)
{
    const struct claw_tool **p;

    if (!__start_claw_tools || !__stop_claw_tools) {
        return CLAW_OK;
    }

    claw_for_each_registered(p, __start_claw_tools,
                             __stop_claw_tools) {
        claw_err_t err = claw_tool_core_register(
            (struct claw_tool *)*p);
        if (err != CLAW_OK) {
            CLAW_LOGE(TAG, "failed to register %s: %s",
                      (*p)->name, claw_strerror(err));
        }
    }

    return CLAW_OK;
}
