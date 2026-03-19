/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Tool class — OOP base class for LLM-callable tools.
 */

#ifndef CLAW_CORE_CLAW_TOOL_H
#define CLAW_CORE_CLAW_TOOL_H

#include "claw/core/claw_errno.h"
#include "claw/core/claw_class.h"
#include <stdint.h>

struct cJSON;

/* ------------------------------------------------------------------ */
/* Tool ops vtable                                                    */
/* ------------------------------------------------------------------ */

struct claw_tool;

struct claw_tool_ops {
    claw_err_t (*execute)(struct claw_tool *tool,
                          const struct cJSON *params,
                          struct cJSON *result);
    claw_err_t (*validate_params)(struct claw_tool *tool,
                                  const struct cJSON *params);
    claw_err_t (*init)(struct claw_tool *tool);     /* NULL = no-op */
    void       (*cleanup)(struct claw_tool *tool);  /* NULL = no-op */
};

/* ------------------------------------------------------------------ */
/* Tool base class                                                    */
/* ------------------------------------------------------------------ */

struct claw_tool {
    const char                *name;
    const char                *description;
    const char                *input_schema_json;
    const struct claw_tool_ops *ops;
    uint8_t                    required_caps;
    uint8_t                    flags;
    claw_list_node_t           node;
};

/* ------------------------------------------------------------------ */
/* Tool core API                                                      */
/* ------------------------------------------------------------------ */

claw_err_t claw_tool_core_register(struct claw_tool *tool);
claw_err_t claw_tool_core_collect_from_section(void);

#endif /* CLAW_CORE_CLAW_TOOL_H */
