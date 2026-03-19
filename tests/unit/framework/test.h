/*
 * SPDX-License-Identifier: MIT
 * Minimal unit test framework for bare-metal ARM semihosting.
 */

#ifndef RTCLAW_TEST_H
#define RTCLAW_TEST_H

#include <stdio.h>
#include <string.h>

static int __attribute__((unused)) _test_total;
static int __attribute__((unused)) _test_fail;
static int __attribute__((unused)) _test_current_failed;

#define TEST_BEGIN() do { _test_total = 0; _test_fail = 0; } while (0)

#define TEST_END() do {                                            \
    printf("\n%d/%d tests passed\n", _test_total - _test_fail,     \
           _test_total);                                           \
    return _test_fail ? 1 : 0;                                     \
} while (0)

#define RUN_TEST(fn) do {                                          \
    _test_current_failed = 0;                                      \
    _test_total++;                                                 \
    fn();                                                          \
    if (_test_current_failed) {                                    \
        printf("  FAIL: %s\n", #fn);                               \
        _test_fail++;                                              \
    } else {                                                       \
        printf("  PASS: %s\n", #fn);                               \
    }                                                              \
} while (0)

#define TEST_ASSERT(cond) do {                                     \
    if (!(cond)) {                                                 \
        printf("    ASSERT(%s) failed at %s:%d\n",                 \
               #cond, __FILE__, __LINE__);                         \
        _test_current_failed = 1;                                  \
        return;                                                    \
    }                                                              \
} while (0)

#define TEST_ASSERT_EQ(a, b) do {                                  \
    long _a = (long)(a), _b = (long)(b);                           \
    if (_a != _b) {                                                \
        printf("    ASSERT_EQ(%s, %s) failed: %ld != %ld"          \
               " at %s:%d\n", #a, #b, _a, _b,                     \
               __FILE__, __LINE__);                                \
        _test_current_failed = 1;                                  \
        return;                                                    \
    }                                                              \
} while (0)

#define TEST_ASSERT_STR_EQ(a, b) do {                              \
    const char *_a = (a), *_b = (b);                               \
    if (!_a || !_b || strcmp(_a, _b) != 0) {                       \
        printf("    ASSERT_STR_EQ(%s, %s) failed: \"%s\" != "     \
               "\"%s\" at %s:%d\n", #a, #b,                       \
               _a ? _a : "(null)", _b ? _b : "(null)",             \
               __FILE__, __LINE__);                                \
        _test_current_failed = 1;                                  \
        return;                                                    \
    }                                                              \
} while (0)

#define TEST_ASSERT_NOT_NULL(ptr) do {                             \
    if ((ptr) == NULL) {                                           \
        printf("    ASSERT_NOT_NULL(%s) failed at %s:%d\n",        \
               #ptr, __FILE__, __LINE__);                          \
        _test_current_failed = 1;                                  \
        return;                                                    \
    }                                                              \
} while (0)

#define TEST_ASSERT_NULL(ptr) do {                                 \
    if ((ptr) != NULL) {                                           \
        printf("    ASSERT_NULL(%s) failed at %s:%d\n",            \
               #ptr, __FILE__, __LINE__);                          \
        _test_current_failed = 1;                                  \
        return;                                                    \
    }                                                              \
} while (0)

#endif /* RTCLAW_TEST_H */
