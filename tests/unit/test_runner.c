/*
 * SPDX-License-Identifier: MIT
 * Unit test runner — aggregates all test suites.
 */

#include "test_runner.h"

#include <stdio.h>

/* Suite entry points (each returns 0=pass, 1=fail) */
extern int test_ai_memory_suite(void);
extern int test_gateway_suite(void);
extern int test_tools_suite(void);

int run_all_unit_tests(void)
{
    int failed = 0;

    printf("\n========== rt-claw unit tests ==========\n\n");

    failed += test_ai_memory_suite();
    failed += test_gateway_suite();
    failed += test_tools_suite();

    printf("\n========================================\n");
    if (failed) {
        printf("FAILED: %d suite(s) had failures\n", failed);
    } else {
        printf("ALL SUITES PASSED\n");
    }

    return failed ? 1 : 0;
}
