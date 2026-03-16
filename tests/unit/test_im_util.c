/*
 * SPDX-License-Identifier: MIT
 * Unit tests for claw/services/im/im_util.c — message chunking.
 */

#include "framework/test.h"
#include "claw/services/im/im_util.h"

#include <string.h>

/* ---- im_find_chunk_end() tests ---- */

static void test_chunk_no_split_needed(void)
{
    /* Message fits in max_chunk — return full length */
    TEST_ASSERT_EQ(im_find_chunk_end("hello", 5, 100), 5);
    TEST_ASSERT_EQ(im_find_chunk_end("hello", 5, 5), 5);
}

static void test_chunk_exact_boundary(void)
{
    /* Message exactly at max_chunk — no split */
    const char *msg = "abcdefghij";   /* 10 bytes */
    TEST_ASSERT_EQ(im_find_chunk_end(msg, 10, 10), 10);
}

static void test_chunk_hard_split(void)
{
    /* No newline in scan range — hard split at max_chunk */
    const char *msg = "aaaaaaaaaabbbbbbbbbb";   /* 20 bytes, no \n */
    TEST_ASSERT_EQ(im_find_chunk_end(msg, 20, 10), 10);
}

static void test_chunk_split_at_newline(void)
{
    /* Newline in the second half — split after it */
    char msg[20];
    memset(msg, 'a', sizeof(msg));
    msg[7] = '\n';   /* newline at position 7, within [5, 10] */

    TEST_ASSERT_EQ(im_find_chunk_end(msg, 20, 10), 8);
}

static void test_chunk_split_last_newline(void)
{
    /* Multiple newlines — pick the last one before max_chunk */
    char msg[20];
    memset(msg, 'a', sizeof(msg));
    msg[6] = '\n';
    msg[8] = '\n';   /* two newlines; scan from 10 backward hits 8 first */

    TEST_ASSERT_EQ(im_find_chunk_end(msg, 20, 10), 9);
}

static void test_chunk_newline_at_max(void)
{
    /* Newline exactly at max_chunk position — included in this chunk */
    char msg[20];
    memset(msg, 'a', sizeof(msg));
    msg[10] = '\n';

    TEST_ASSERT_EQ(im_find_chunk_end(msg, 20, 10), 11);
}

static void test_chunk_newline_in_first_half_ignored(void)
{
    /* Newline only in first half — not in scan range, hard split */
    char msg[20];
    memset(msg, 'a', sizeof(msg));
    msg[2] = '\n';   /* position 2 < max_chunk/2 (5), outside scan */

    TEST_ASSERT_EQ(im_find_chunk_end(msg, 20, 10), 10);
}

static void test_chunk_single_byte_remaining(void)
{
    TEST_ASSERT_EQ(im_find_chunk_end("x", 1, 100), 1);
    TEST_ASSERT_EQ(im_find_chunk_end("x", 1, 1), 1);
}

static void test_chunk_zero_remaining(void)
{
    TEST_ASSERT_EQ(im_find_chunk_end("", 0, 100), 0);
}

static void test_chunk_multi_round(void)
{
    /*
     * Simulate a full chunking loop like send_reply().
     * 25-byte message, max_chunk=10, newline at 7 and 17.
     */
    char msg[25];
    memset(msg, 'a', sizeof(msg));
    msg[7]  = '\n';
    msg[17] = '\n';

    const char *p = msg;
    size_t remaining = 25;
    size_t total_consumed = 0;
    int rounds = 0;

    while (remaining > 0) {
        size_t chunk = im_find_chunk_end(p, remaining, 10);
        TEST_ASSERT(chunk > 0);
        TEST_ASSERT(chunk <= remaining);
        p += chunk;
        remaining -= chunk;
        total_consumed += chunk;
        rounds++;
        TEST_ASSERT(rounds <= 10);  /* safety: avoid infinite loop */
    }

    TEST_ASSERT_EQ(total_consumed, 25);
    /* Round 1: split at \n pos 7 → 8 bytes
     * Round 2: 17 bytes left, text[9]=\n(pos 17-8=9) → find in [5,10]
     *          msg[17] is now at offset 9 from p... scan finds it → 10
     * Round 3: remaining 7 bytes, fits in 10 → 7 */
    TEST_ASSERT(rounds >= 2);
    TEST_ASSERT(rounds <= 4);
}

static void test_chunk_large_max(void)
{
    /* max_chunk larger than remaining — returns remaining */
    const char *msg = "short";
    TEST_ASSERT_EQ(im_find_chunk_end(msg, 5, 4096), 5);
}

static void test_chunk_newline_at_end(void)
{
    /* Newline at the very end of scan range (position max_chunk) */
    char msg[30];
    memset(msg, 'a', sizeof(msg));
    msg[20] = '\n';  /* exactly at max_chunk=20 */

    /* scan from 20 backward, text[20]='\n' → return 21 (include \n) */
    TEST_ASSERT_EQ(im_find_chunk_end(msg, 30, 20), 21);
}

static void test_chunk_max_chunk_one(void)
{
    /* Degenerate case: max_chunk=1, must always return 1 */
    const char *msg = "abc";
    TEST_ASSERT_EQ(im_find_chunk_end(msg, 3, 1), 1);
}

static void test_chunk_max_chunk_two(void)
{
    /* max_chunk=2: scan range [1, 2], check for newline */
    const char *msg = "a\nb";   /* newline at position 1 */
    TEST_ASSERT_EQ(im_find_chunk_end(msg, 3, 2), 2);
}

int test_im_util_suite(void)
{
    printf("=== test_im_util ===\n");
    TEST_BEGIN();

    RUN_TEST(test_chunk_no_split_needed);
    RUN_TEST(test_chunk_exact_boundary);
    RUN_TEST(test_chunk_hard_split);
    RUN_TEST(test_chunk_split_at_newline);
    RUN_TEST(test_chunk_split_last_newline);
    RUN_TEST(test_chunk_newline_at_max);
    RUN_TEST(test_chunk_newline_in_first_half_ignored);
    RUN_TEST(test_chunk_single_byte_remaining);
    RUN_TEST(test_chunk_zero_remaining);
    RUN_TEST(test_chunk_multi_round);
    RUN_TEST(test_chunk_large_max);
    RUN_TEST(test_chunk_newline_at_end);
    RUN_TEST(test_chunk_max_chunk_one);
    RUN_TEST(test_chunk_max_chunk_two);

    TEST_END();
}
