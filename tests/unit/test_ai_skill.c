/*
 * SPDX-License-Identifier: MIT
 * Unit tests for claw/services/ai/ai_skill.c
 */

#include "framework/test.h"

#ifdef CONFIG_RTCLAW_SKILL_ENABLE

#include "claw/services/ai/ai_skill.h"

#include <string.h>

/* Number of built-in skills after ai_skill_init() */
static int s_builtin_count;

/* ---- init / count ---- */

static void test_skill_init(void)
{
    TEST_ASSERT_EQ(ai_skill_init(), CLAW_OK);
    s_builtin_count = ai_skill_count();
    /* At least "greet" is always registered */
    TEST_ASSERT(s_builtin_count >= 1);
}

static void test_skill_init_resets(void)
{
    ai_skill_init();
    ai_skill_register("temp", "temporary", "do %s");
    int before = ai_skill_count();
    TEST_ASSERT(before > s_builtin_count);

    /* Re-init should clear user skills */
    ai_skill_init();
    TEST_ASSERT_EQ(ai_skill_count(), s_builtin_count);
}

/* ---- register ---- */

static void test_skill_register(void)
{
    ai_skill_init();
    int base = ai_skill_count();

    int ret = ai_skill_register("test_sk", "A test skill",
                                "Execute test: %s");
    TEST_ASSERT_EQ(ret, CLAW_OK);
    TEST_ASSERT_EQ(ai_skill_count(), base + 1);
}

static void test_skill_register_duplicate(void)
{
    ai_skill_init();
    ai_skill_register("dup_sk", "first", "first: %s");

    int ret = ai_skill_register("dup_sk", "second", "second: %s");
    TEST_ASSERT(ret != CLAW_OK);
}

static void test_skill_register_null(void)
{
    ai_skill_init();
    TEST_ASSERT(ai_skill_register(NULL, "desc", "tmpl") != CLAW_OK);
    TEST_ASSERT(ai_skill_register("ok", "desc", NULL) != CLAW_OK);
}

static void test_skill_register_overflow(void)
{
    ai_skill_init();
    int base = ai_skill_count();
    int avail = SKILL_MAX - base;

    char name[SKILL_NAME_MAX];
    for (int i = 0; i < avail; i++) {
        snprintf(name, sizeof(name), "sk_%d", i);
        TEST_ASSERT_EQ(
            ai_skill_register(name, "desc", "prompt: %s"),
            CLAW_OK);
    }
    TEST_ASSERT_EQ(ai_skill_count(), SKILL_MAX);

    /* One more should fail */
    TEST_ASSERT(
        ai_skill_register("overflow", "x", "x: %s") != CLAW_OK);
}

/* ---- find ---- */

static void test_skill_find(void)
{
    ai_skill_init();
    ai_skill_register("finder", "find me", "find: %s");

    const char *tmpl = ai_skill_find("finder");
    TEST_ASSERT_NOT_NULL(tmpl);
    TEST_ASSERT_STR_EQ(tmpl, "find: %s");

    TEST_ASSERT_NULL(ai_skill_find("nonexistent"));
    TEST_ASSERT_NULL(ai_skill_find(NULL));
}

static void test_skill_find_builtin(void)
{
    ai_skill_init();
    /* "greet" is always registered as built-in */
    const char *tmpl = ai_skill_find("greet");
    TEST_ASSERT_NOT_NULL(tmpl);
}

/* ---- remove ---- */

static void test_skill_remove(void)
{
    ai_skill_init();
    ai_skill_register("removeme", "temp", "temp: %s");
    int before = ai_skill_count();

    TEST_ASSERT_EQ(ai_skill_remove("removeme"), CLAW_OK);
    TEST_ASSERT_EQ(ai_skill_count(), before - 1);
    TEST_ASSERT_NULL(ai_skill_find("removeme"));
}

static void test_skill_remove_builtin(void)
{
    ai_skill_init();
    /* Built-in "greet" cannot be removed */
    TEST_ASSERT(ai_skill_remove("greet") != CLAW_OK);
    TEST_ASSERT_NOT_NULL(ai_skill_find("greet"));
}

static void test_skill_remove_nonexistent(void)
{
    ai_skill_init();
    TEST_ASSERT(ai_skill_remove("ghost") != CLAW_OK);
    TEST_ASSERT(ai_skill_remove(NULL) != CLAW_OK);
}

/* ---- get_name ---- */

static void test_skill_get_name(void)
{
    ai_skill_init();
    /* Index 0 should be a built-in (first registered) */
    const char *name = ai_skill_get_name(0);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT(strlen(name) > 0);

    /* Out-of-bounds returns NULL */
    TEST_ASSERT_NULL(ai_skill_get_name(-1));
    TEST_ASSERT_NULL(ai_skill_get_name(SKILL_MAX + 1));
}

static void test_skill_get_name_user(void)
{
    ai_skill_init();
    ai_skill_register("myskill", "mine", "do: %s");
    int last = ai_skill_count() - 1;

    const char *name = ai_skill_get_name(last);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_STR_EQ(name, "myskill");
}

/* ---- list_to_buf ---- */

static void test_skill_list_to_buf(void)
{
    ai_skill_init();
    ai_skill_register("listme", "listed skill", "list: %s");

    char buf[512];
    int written = ai_skill_list_to_buf(buf, sizeof(buf));

    TEST_ASSERT(written > 0);
    /* Should contain header with count */
    TEST_ASSERT(strstr(buf, "skills:") != NULL);
    /* Should contain skill names */
    TEST_ASSERT(strstr(buf, "greet") != NULL);
    TEST_ASSERT(strstr(buf, "listme") != NULL);
    /* Built-in should be marked */
    TEST_ASSERT(strstr(buf, "(built-in)") != NULL);
}

static void test_skill_list_to_buf_small(void)
{
    ai_skill_init();

    /* Tiny buffer — should not crash, just truncate */
    char buf[8];
    int written = ai_skill_list_to_buf(buf, sizeof(buf));
    TEST_ASSERT(written >= 0);
    TEST_ASSERT(buf[sizeof(buf) - 1] == '\0' || written < (int)sizeof(buf));
}

static void test_skill_list_to_buf_null(void)
{
    TEST_ASSERT_EQ(ai_skill_list_to_buf(NULL, 100), 0);

    char buf[32];
    TEST_ASSERT_EQ(ai_skill_list_to_buf(buf, 0), 0);
}

/* ---- try_command ---- */

static void test_skill_try_command_not_found(void)
{
    ai_skill_init();

    char reply[256];
    char *argv[] = { "/nonexistent", "arg1" };
    int ret = ai_skill_try_command(argv[0], 2, argv,
                                   reply, sizeof(reply));
    TEST_ASSERT(ret != CLAW_OK);
}

static void test_skill_try_command_strips_slash(void)
{
    ai_skill_init();

    char reply[256];
    /* "/greet" exists as built-in (without slash) */
    char *argv[] = { "/ghost_skill" };
    int ret = ai_skill_try_command(argv[0], 1, argv,
                                   reply, sizeof(reply));
    /* "ghost_skill" does not exist → CLAW_ERROR */
    TEST_ASSERT(ret != CLAW_OK);

    /* Register "ghost_skill" and confirm it would be found */
    ai_skill_register("ghost_skill", "test", "test: %s");
    TEST_ASSERT_NOT_NULL(ai_skill_find("ghost_skill"));
}

static void test_skill_try_command_null_args(void)
{
    char reply[128];
    char *argv[] = { "/test" };

    TEST_ASSERT(ai_skill_try_command(NULL, 1, argv,
                                     reply, sizeof(reply)) != CLAW_OK);
    TEST_ASSERT(ai_skill_try_command("/test", 1, argv,
                                     NULL, 128) != CLAW_OK);
    TEST_ASSERT(ai_skill_try_command("/test", 1, argv,
                                     reply, 0) != CLAW_OK);
}

static void test_skill_try_command_no_slash(void)
{
    ai_skill_init();
    ai_skill_register("bare", "bare skill", "bare: %s");

    /* Without slash — should still resolve */
    /*
     * We can only test that find succeeds (bare exists).
     * Actual execution calls ai_chat_raw which needs AI engine.
     */
    TEST_ASSERT_NOT_NULL(ai_skill_find("bare"));
}

/* ---- build_summary ---- */

static void test_skill_build_summary(void)
{
    ai_skill_init();
    char *summary = ai_skill_build_summary();

    /* Built-in skills exist, so summary should not be NULL */
    TEST_ASSERT_NOT_NULL(summary);
    TEST_ASSERT(strstr(summary, "greet") != NULL);
    TEST_ASSERT(strstr(summary, "Available skills") != NULL);

    claw_free(summary);
}

static void test_skill_build_summary_empty(void)
{
    /*
     * Cannot test truly empty (builtins always register),
     * but summary should at least be non-NULL when skills exist.
     */
    ai_skill_init();
    char *summary = ai_skill_build_summary();
    TEST_ASSERT_NOT_NULL(summary);
    claw_free(summary);
}

/* ---- Suite ---- */

int test_ai_skill_suite(void)
{
    printf("=== test_ai_skill ===\n");
    TEST_BEGIN();

    /* init / count */
    RUN_TEST(test_skill_init);
    RUN_TEST(test_skill_init_resets);

    /* register */
    RUN_TEST(test_skill_register);
    RUN_TEST(test_skill_register_duplicate);
    RUN_TEST(test_skill_register_null);
    RUN_TEST(test_skill_register_overflow);

    /* find */
    RUN_TEST(test_skill_find);
    RUN_TEST(test_skill_find_builtin);

    /* remove */
    RUN_TEST(test_skill_remove);
    RUN_TEST(test_skill_remove_builtin);
    RUN_TEST(test_skill_remove_nonexistent);

    /* get_name */
    RUN_TEST(test_skill_get_name);
    RUN_TEST(test_skill_get_name_user);

    /* list_to_buf */
    RUN_TEST(test_skill_list_to_buf);
    RUN_TEST(test_skill_list_to_buf_small);
    RUN_TEST(test_skill_list_to_buf_null);

    /* try_command */
    RUN_TEST(test_skill_try_command_not_found);
    RUN_TEST(test_skill_try_command_strips_slash);
    RUN_TEST(test_skill_try_command_null_args);
    RUN_TEST(test_skill_try_command_no_slash);

    /* build_summary */
    RUN_TEST(test_skill_build_summary);
    RUN_TEST(test_skill_build_summary_empty);

    TEST_END();
}

#else /* !CONFIG_RTCLAW_SKILL_ENABLE */

int test_ai_skill_suite(void)
{
    printf("=== test_ai_skill === (SKIPPED: skill not enabled)\n");
    return 0;
}

#endif /* CONFIG_RTCLAW_SKILL_ENABLE */
