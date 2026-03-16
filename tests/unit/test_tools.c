/*
 * SPDX-License-Identifier: MIT
 * Unit tests for claw/tools/claw_tools.c
 */

#include "framework/test.h"
#include "claw/tools/claw_tools.h"
#include "cJSON.h"

/* Dummy tool execute function */
static int dummy_exec(const cJSON *params, cJSON *result)
{
    (void)params;
    cJSON_AddStringToObject(result, "status", "ok");
    return CLAW_OK;
}

static int s_builtin_count;

static void test_tools_init(void)
{
    TEST_ASSERT_EQ(claw_tools_init(), CLAW_OK);
    /* Platform may auto-register built-in tools (system, net, etc.) */
    s_builtin_count = claw_tools_count();
    TEST_ASSERT(s_builtin_count >= 0);
}

static void test_tool_register(void)
{
    claw_tools_init();
    int base = claw_tools_count();

    int ret = claw_tool_register(
        "test_tool", "A test tool",
        "{\"type\":\"object\",\"properties\":{}}",
        dummy_exec, 0, 0);
    TEST_ASSERT_EQ(ret, CLAW_OK);
    TEST_ASSERT_EQ(claw_tools_count(), base + 1);
}

static void test_tool_find(void)
{
    claw_tools_init();
    claw_tool_register("alpha", "first", "{}", dummy_exec, 0, 0);
    claw_tool_register("beta", "second", "{}", dummy_exec, 0, 0);

    const claw_tool_t *t = claw_tool_find("beta");
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_STR_EQ(t->name, "beta");

    TEST_ASSERT_NULL(claw_tool_find("gamma"));
}

static void test_tool_get_by_index(void)
{
    claw_tools_init();
    int base = claw_tools_count();
    claw_tool_register("idx0", "zero", "{}", dummy_exec, 0, 0);
    claw_tool_register("idx1", "one", "{}", dummy_exec, 0, 0);

    const claw_tool_t *t0 = claw_tool_get(base);
    TEST_ASSERT_NOT_NULL(t0);
    TEST_ASSERT_STR_EQ(t0->name, "idx0");

    const claw_tool_t *t1 = claw_tool_get(base + 1);
    TEST_ASSERT_NOT_NULL(t1);
    TEST_ASSERT_STR_EQ(t1->name, "idx1");

    TEST_ASSERT_NULL(claw_tool_get(base + 2));
    TEST_ASSERT_NULL(claw_tool_get(-1));
}

static void test_tools_to_json(void)
{
    claw_tools_init();
    int base = claw_tools_count();
    claw_tool_register("ping", "ping tool", "{}", dummy_exec, 0, 0);
    claw_tool_register("pong", "pong tool", "{}", dummy_exec, 0, 0);

    cJSON *arr = claw_tools_to_json();
    TEST_ASSERT_NOT_NULL(arr);
    TEST_ASSERT_EQ(cJSON_GetArraySize(arr), base + 2);

    /* Our registered tools are at the end */
    cJSON *t = cJSON_GetArrayItem(arr, base);
    TEST_ASSERT_STR_EQ(
        cJSON_GetObjectItem(t, "name")->valuestring, "ping");

    cJSON_Delete(arr);
}

static void test_tools_to_json_exclude(void)
{
    claw_tools_init();
    int base = claw_tools_count();
    claw_tool_register("lcd_draw", "draw", "{}", dummy_exec, 0, 0);
    claw_tool_register("lcd_clear", "clear", "{}", dummy_exec, 0, 0);
    claw_tool_register("http_get_test", "get", "{}", dummy_exec, 0, 0);

    cJSON *arr = claw_tools_to_json_exclude("lcd_");
    TEST_ASSERT_NOT_NULL(arr);
    /* All builtins + http_get_test, minus 2 lcd_ tools */
    TEST_ASSERT_EQ(cJSON_GetArraySize(arr), base + 1);

    /* Verify no lcd_ tools in result */
    int lcd_count = 0;
    for (int i = 0; i < cJSON_GetArraySize(arr); i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        const char *n = cJSON_GetObjectItem(item, "name")->valuestring;
        if (strncmp(n, "lcd_", 4) == 0) {
            lcd_count++;
        }
    }
    TEST_ASSERT_EQ(lcd_count, 0);

    cJSON_Delete(arr);
}

static void test_tool_register_overflow(void)
{
    claw_tools_init();
    int base = claw_tools_count();
    int avail = CLAW_TOOL_MAX - base;

    char name[32];
    for (int i = 0; i < avail; i++) {
        snprintf(name, sizeof(name), "tool_%d", i);
        TEST_ASSERT_EQ(
            claw_tool_register(name, "desc", "{}", dummy_exec, 0, 0),
            CLAW_OK);
    }
    TEST_ASSERT_EQ(claw_tools_count(), CLAW_TOOL_MAX);

    /* One more should fail */
    TEST_ASSERT(
        claw_tool_register("overflow", "x", "{}", dummy_exec, 0, 0)
        != CLAW_OK);
}

int test_tools_suite(void)
{
    printf("=== test_tools ===\n");
    TEST_BEGIN();

    RUN_TEST(test_tools_init);
    RUN_TEST(test_tool_register);
    RUN_TEST(test_tool_find);
    RUN_TEST(test_tool_get_by_index);
    RUN_TEST(test_tools_to_json);
    RUN_TEST(test_tools_to_json_exclude);
    RUN_TEST(test_tool_register_overflow);

    TEST_END();
}
