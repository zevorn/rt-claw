/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Script execution tool.
 */

#include "claw/services/tools/tools.h"
#include "platform/scripting.h"

#include <stdio.h>
#include <string.h>

#define SCRIPT_OUTPUT_MAX 1024
#define SCRIPT_LANGUAGE_MICROPYTHON "micropython"

__attribute__((weak))
int claw_platform_script_supported(const char *language)
{
    (void)language;
    return 0;
}

__attribute__((weak))
int claw_platform_run_script(const char *language, const char *code,
                             char *output, size_t output_size)
{
    (void)language;
    (void)code;
    if (output && output_size > 0) {
        output[0] = '\0';
    }
    return -1;
}

static const char *script_language_from_params(const cJSON *params)
{
    const cJSON *language = cJSON_GetObjectItem(params, "language");

    if (language && cJSON_IsString(language)) {
        return language->valuestring;
    }

    return SCRIPT_LANGUAGE_MICROPYTHON;
}

static claw_err_t tool_run_script_validate(struct claw_tool *tool,
                                           const cJSON *params)
{
    const cJSON *code;
    const cJSON *language;

    (void)tool;
    if (!params) {
        return CLAW_ERR_INVALID;
    }

    code = cJSON_GetObjectItem(params, "code");
    language = cJSON_GetObjectItem(params, "language");

    if (!code || !cJSON_IsString(code)) {
        return CLAW_ERR_INVALID;
    }

    if (language && !cJSON_IsString(language)) {
        return CLAW_ERR_INVALID;
    }

    return CLAW_OK;
}

static claw_err_t tool_run_script_execute(struct claw_tool *tool,
                                          const cJSON *params,
                                          cJSON *result)
{
    const cJSON *code = cJSON_GetObjectItem(params, "code");
    const char *language = script_language_from_params(params);
    char *output;

    (void)tool;
    output = claw_malloc(SCRIPT_OUTPUT_MAX);
    if (!output) {
        cJSON_AddStringToObject(result, "error", "out of memory");
        return CLAW_OK;
    }
    output[0] = '\0';

    if (!claw_platform_script_supported(language)) {
        char msg[64];

        snprintf(msg, sizeof(msg), "runtime '%s' not supported", language);
        cJSON_AddStringToObject(result, "error", msg);
        claw_free(output);
        return CLAW_OK;
    }

    if (claw_platform_run_script(language, code->valuestring,
                                 output, SCRIPT_OUTPUT_MAX) != 0) {
        cJSON_AddStringToObject(result, "error",
                                output[0] ? output
                                          : "script execution failed");
        claw_free(output);
        return CLAW_OK;
    }

    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddStringToObject(result, "language", language);
    cJSON_AddStringToObject(result, "output", output);
    if (strlen(output) >= SCRIPT_OUTPUT_MAX - 1) {
        cJSON_AddBoolToObject(result, "truncated", 1);
    }

    claw_free(output);
    return CLAW_OK;
}

static const char schema_run_script[] =
    "{\"type\":\"object\","
    "\"properties\":{"
    "\"language\":{\"type\":\"string\","
    "\"description\":\"Scripting runtime. Defaults to micropython.\"},"
    "\"code\":{\"type\":\"string\","
    "\"description\":\"Script source code to execute on-device.\"}},"
    "\"required\":[\"code\"]}";

static const struct claw_tool_ops run_script_ops = {
    .execute = tool_run_script_execute,
    .validate_params = tool_run_script_validate,
};

static struct claw_tool run_script_tool = {
    .name = "run_script",
    .description =
        "Execute a short on-device script and return stdout or "
        "exception output. RT-Thread uses MicroPython, Linux uses Python.",
    .input_schema_json = schema_run_script,
    .ops = &run_script_ops,
    .flags = CLAW_TOOL_LOCAL_ONLY,
};

CLAW_TOOL_REGISTER(run_script, &run_script_tool);
