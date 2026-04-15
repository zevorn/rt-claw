# SPDX-License-Identifier: MIT
# Unit test library — compiled as static lib, linked by SCons with RT-Thread.

unit_test_src = files(
    'test_runner.c',
    'test_ai_memory.c',
    'test_gateway.c',
    'test_tools.c',
    'test_script_tool.c',
    'test_im_util.c',
)

test_defs = platform_defs

if get_option('ota')
    test_defs += '-DCONFIG_RTCLAW_OTA_ENABLE'
endif

if get_option('skill')
    test_defs += '-DCONFIG_RTCLAW_SKILL_ENABLE'
endif

# OTA and skill tests always compiled; guarded by #ifdef inside.
unit_test_src += files('test_ota.c')
unit_test_src += files('test_ai_skill.c')

unittest_lib = static_library('unittest',
    unit_test_src,
    include_directories : [claw_inc, claw_root_inc, cjson_inc, platform_incs],
    c_args : test_defs,
)
