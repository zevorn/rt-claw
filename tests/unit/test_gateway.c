/*
 * SPDX-License-Identifier: MIT
 * Unit tests for claw/core/gateway.c
 */

#include "framework/test.h"
#include "claw/core/gateway.h"

static void test_gateway_init(void)
{
    TEST_ASSERT_EQ(gateway_init(), CLAW_OK);
}

static void test_gateway_register_service(void)
{
    gateway_init();

    struct claw_mq *mq = claw_mq_create("test", sizeof(struct gateway_msg), 4);
    TEST_ASSERT_NOT_NULL(mq);

    int ret = gateway_register_service(
        "svc_a", (1 << GW_MSG_DATA) | (1 << GW_MSG_CMD), mq);
    TEST_ASSERT_EQ(ret, CLAW_OK);
}

static void test_gateway_stats_init(void)
{
    gateway_init();

    struct gateway_stats st;
    gateway_get_stats(&st);
    TEST_ASSERT_EQ(st.total, 0);
    TEST_ASSERT_EQ(st.dropped, 0);
    TEST_ASSERT_EQ(st.no_consumer, 0);
}

static void test_gateway_register_overflow(void)
{
    gateway_init();

    for (int i = 0; i < GW_MAX_SERVICES; i++) {
        struct claw_mq *mq = claw_mq_create("q", sizeof(struct gateway_msg), 2);
        int ret = gateway_register_service("svc", 0xFF, mq);
        TEST_ASSERT_EQ(ret, CLAW_OK);
    }

    /* One more should fail */
    struct claw_mq *mq = claw_mq_create("q", sizeof(struct gateway_msg), 2);
    int ret = gateway_register_service("over", 0xFF, mq);
    TEST_ASSERT(ret != CLAW_OK);
}

/* Pass-through handler: always returns 0 */
static int handler_passthrough(struct gateway_msg *msg)
{
    (void)msg;
    return 0;
}

/* Consumer handler: returns 1 to consume the message */
static int handler_consume(struct gateway_msg *msg)
{
    (void)msg;
    return 1;
}

static void test_handler_register(void)
{
    gateway_init();

    TEST_ASSERT_EQ(
        gateway_register_handler("pass", handler_passthrough), CLAW_OK);
    TEST_ASSERT_EQ(
        gateway_register_handler("consume", handler_consume), CLAW_OK);
}

static void test_handler_register_overflow(void)
{
    gateway_init();

    for (int i = 0; i < GW_MAX_HANDLERS; i++) {
        TEST_ASSERT_EQ(
            gateway_register_handler("h", handler_passthrough), CLAW_OK);
    }

    TEST_ASSERT(
        gateway_register_handler("over", handler_passthrough) != CLAW_OK);
}

static void test_pipeline_stats_filtered(void)
{
    gateway_init();

    struct gateway_stats st;
    gateway_get_stats(&st);
    TEST_ASSERT_EQ(st.filtered, 0);
}

int test_gateway_suite(void)
{
    printf("=== test_gateway ===\n");
    TEST_BEGIN();

    RUN_TEST(test_gateway_init);
    RUN_TEST(test_gateway_register_service);
    RUN_TEST(test_gateway_stats_init);
    RUN_TEST(test_gateway_register_overflow);
    RUN_TEST(test_handler_register);
    RUN_TEST(test_handler_register_overflow);
    RUN_TEST(test_pipeline_stats_filtered);

    TEST_END();
}
