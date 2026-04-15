/*
 * SPDX-License-Identifier: MIT
 * Unit tests for the run_script tool.
 */

#include "framework/test.h"
#include "claw/core/tool.h"
#include "claw/services/tools/tools.h"
#include "cJSON.h"

#include <stdio.h>

static void test_run_script_tool_registered(void)
{
    claw_tool_core_collect_from_section();
    TEST_ASSERT_EQ(claw_tools_init(), CLAW_OK);
    TEST_ASSERT_NOT_NULL(claw_tool_find("run_script"));
}

static void test_run_script_tool_executes_code(void)
{
    const struct claw_tool *tool = claw_tool_find("run_script");
    cJSON *params;
    cJSON *result;
    cJSON *status;
    cJSON *output;

    TEST_ASSERT_NOT_NULL(tool);

    params = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(params);
    cJSON_AddStringToObject(params, "code", "print(40 + 2)");

    result = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(result);

    TEST_ASSERT_EQ(
        claw_tool_invoke((struct claw_tool *)tool, params, result),
        CLAW_OK
    );

    status = cJSON_GetObjectItem(result, "status");
    output = cJSON_GetObjectItem(result, "output");
    TEST_ASSERT_NOT_NULL(status);
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_STR_EQ(status->valuestring, "ok");
    TEST_ASSERT(strstr(output->valuestring, "42") != NULL);

    cJSON_Delete(result);
    cJSON_Delete(params);
}

#ifdef CLAW_PLATFORM_LINUX
static void test_run_script_tool_executes_python(void)
{
    const struct claw_tool *tool = claw_tool_find("run_script");
    cJSON *params;
    cJSON *result;
    cJSON *status;
    cJSON *output;

    TEST_ASSERT_NOT_NULL(tool);

    params = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(params);
    cJSON_AddStringToObject(params, "language", "python");
    cJSON_AddStringToObject(params, "code", "print(40 + 2)");

    result = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(result);

    TEST_ASSERT_EQ(
        claw_tool_invoke((struct claw_tool *)tool, params, result),
        CLAW_OK
    );

    status = cJSON_GetObjectItem(result, "status");
    output = cJSON_GetObjectItem(result, "output");
    TEST_ASSERT_NOT_NULL(status);
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_STR_EQ(status->valuestring, "ok");
    TEST_ASSERT(strstr(output->valuestring, "42") != NULL);

    cJSON_Delete(result);
    cJSON_Delete(params);
}
#endif

int test_script_tool_suite(void)
{
    printf("=== test_script_tool ===\n");
    TEST_BEGIN();

    RUN_TEST(test_run_script_tool_registered);
    RUN_TEST(test_run_script_tool_executes_code);
#ifdef CLAW_PLATFORM_LINUX
    RUN_TEST(test_run_script_tool_executes_python);
#endif

    TEST_END();
}
