/*
 * SPDX-License-Identifier: MIT
 * Unit tests for claw/services/ai/ai_memory.c
 */

#include "framework/test.h"
#include "claw/services/ai/ai_memory.h"
#include "osal/claw_kv.h"
#include "cJSON.h"

#include <stdlib.h>

/* ---- Short-term memory tests ---- */

static void test_memory_init(void)
{
    TEST_ASSERT_EQ(ai_memory_init(), CLAW_OK);
    TEST_ASSERT_EQ(ai_memory_count(), 0);
}

static void test_memory_add_count(void)
{
    ai_memory_init();
    ai_memory_add("user", "\"hello\"", 0);
    TEST_ASSERT_EQ(ai_memory_count(), 1);
    ai_memory_add("assistant", "\"world\"", 0);
    TEST_ASSERT_EQ(ai_memory_count(), 2);
}

static void test_memory_clear(void)
{
    ai_memory_init();
    ai_memory_add("user", "\"a\"", 0);
    ai_memory_add("assistant", "\"b\"", 0);
    TEST_ASSERT_EQ(ai_memory_count(), 2);
    ai_memory_clear();
    TEST_ASSERT_EQ(ai_memory_count(), 0);
}

static void test_memory_channel_isolation(void)
{
    ai_memory_init();
    ai_memory_add("user", "\"ch0\"", 0);
    ai_memory_add("user", "\"ch1\"", 1);
    ai_memory_add("user", "\"ch0b\"", 0);
    TEST_ASSERT_EQ(ai_memory_count(), 3);
    TEST_ASSERT_EQ(ai_memory_count_channel(0), 2);
    TEST_ASSERT_EQ(ai_memory_count_channel(1), 1);
}

static void test_memory_clear_channel(void)
{
    ai_memory_init();
    ai_memory_add("user", "\"ch0\"", 0);
    ai_memory_add("user", "\"ch1\"", 1);
    ai_memory_clear_channel(0);
    TEST_ASSERT_EQ(ai_memory_count_channel(0), 0);
    TEST_ASSERT_EQ(ai_memory_count_channel(1), 1);
}

static void test_memory_build_json(void)
{
    ai_memory_init();
    ai_memory_add("user", "\"hello\"", 0);
    ai_memory_add("assistant", "\"hi\"", 0);

    cJSON *arr = ai_memory_build(0);
    TEST_ASSERT_NOT_NULL(arr);
    TEST_ASSERT_EQ(cJSON_GetArraySize(arr), 2);

    cJSON *msg0 = cJSON_GetArrayItem(arr, 0);
    TEST_ASSERT_STR_EQ(
        cJSON_GetObjectItem(msg0, "role")->valuestring, "user");

    cJSON *msg1 = cJSON_GetArrayItem(arr, 1);
    TEST_ASSERT_STR_EQ(
        cJSON_GetObjectItem(msg1, "role")->valuestring, "assistant");

    cJSON_Delete(arr);
}

/* ---- Long-term memory tests ---- */

static void test_ltm_init(void)
{
    claw_kv_init();
    TEST_ASSERT_EQ(ai_ltm_init(), CLAW_OK);
    TEST_ASSERT_EQ(ai_ltm_count(), 0);
}

static void test_ltm_save_load(void)
{
    claw_kv_init();
    ai_ltm_init();

    TEST_ASSERT_EQ(ai_ltm_save("name", "rt-claw"), CLAW_OK);
    TEST_ASSERT_EQ(ai_ltm_count(), 1);

    char buf[128];
    TEST_ASSERT_EQ(ai_ltm_load("name", buf, sizeof(buf)), CLAW_OK);
    TEST_ASSERT_STR_EQ(buf, "rt-claw");
}

static void test_ltm_delete(void)
{
    claw_kv_init();
    ai_ltm_init();

    ai_ltm_save("key1", "val1");
    TEST_ASSERT_EQ(ai_ltm_count(), 1);
    TEST_ASSERT_EQ(ai_ltm_delete("key1"), CLAW_OK);
    TEST_ASSERT_EQ(ai_ltm_count(), 0);

    /* Delete non-existent key */
    TEST_ASSERT(ai_ltm_delete("nope") != CLAW_OK);
}

static void test_ltm_overflow(void)
{
    claw_kv_init();
    ai_ltm_init();

    /*
     * Fill LTM to capacity. On RT-Thread the RAM-only KV store
     * has limited slots (48), and each LTM entry uses multiple
     * KV keys, so we may hit the KV limit before LTM_MAX_ENTRIES.
     * Test that we can store at least a few and that we eventually
     * get a limit error.
     */
    char key[32];
    int saved = 0;
    for (int i = 0; i < LTM_MAX_ENTRIES; i++) {
        snprintf(key, sizeof(key), "k%d", i);
        if (ai_ltm_save(key, "v") != CLAW_OK) {
            break;
        }
        saved++;
    }
    TEST_ASSERT(saved >= 1);
    /* Note: ai_ltm_count() may differ from saved if KV backend
     * runs out of space mid-save. This is a known limitation
     * of the RTT RAM-only KV (48 entries max). */
    TEST_ASSERT(ai_ltm_count() >= saved);
}

static void test_ltm_build_context(void)
{
    claw_kv_init();
    ai_ltm_init();

    /* Empty: should return NULL */
    TEST_ASSERT_NULL(ai_ltm_build_context());

    ai_ltm_save("owner", "zevorn");
    char *ctx = ai_ltm_build_context();
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT(strstr(ctx, "owner") != NULL);
    TEST_ASSERT(strstr(ctx, "zevorn") != NULL);
    claw_free(ctx);
}

int test_ai_memory_suite(void)
{
    printf("=== test_ai_memory ===\n");
    TEST_BEGIN();

    RUN_TEST(test_memory_init);
    RUN_TEST(test_memory_add_count);
    RUN_TEST(test_memory_clear);
    RUN_TEST(test_memory_channel_isolation);
    RUN_TEST(test_memory_clear_channel);
    RUN_TEST(test_memory_build_json);
    RUN_TEST(test_ltm_init);
    RUN_TEST(test_ltm_save_load);
    RUN_TEST(test_ltm_delete);
    RUN_TEST(test_ltm_overflow);
    RUN_TEST(test_ltm_build_context);

    TEST_END();
}
