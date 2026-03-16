/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Platform entry for QEMU vexpress-a9 / RT-Thread.
 */

#include <rtthread.h>
#include "osal/claw_os.h"

#ifdef RTCLAW_UNIT_TEST
#include "tests/unit/test_runner.h"
#include "tests/unit/framework/semihosting.h"
#else
#include "claw/claw_init.h"
#include "claw_board.h"
#endif

int main(void)
{
    claw_log_set_enabled(1);

#ifdef RTCLAW_UNIT_TEST
    int rc = run_all_unit_tests();

    /* Give RT-Thread time to flush console output */
    rt_thread_mdelay(200);

    semihosting_exit(rc);
    return rc;
#else
    board_early_init();
    claw_init();
    return 0;
#endif
}
