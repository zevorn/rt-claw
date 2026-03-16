# SPDX-License-Identifier: MIT
# Unit test library — compiled as static lib, linked by SCons with RT-Thread.

unit_test_src = files(
    'test_runner.c',
    'test_ai_memory.c',
    'test_gateway.c',
    'test_tools.c',
)

unittest_lib = static_library('unittest',
    unit_test_src,
    include_directories : [claw_inc, claw_root_inc, cjson_inc, platform_incs],
    c_args : platform_defs,
)
