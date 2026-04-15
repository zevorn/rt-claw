# SPDX-License-Identifier: MIT
# Linux native platform: link claw + osal + cjson → executable

platform_src = files(
    'main.c',
    'shell.c',
    'board.c',
    'ota_stub.c',
)

if get_option('tool_script')
    platform_src += files('scripting.c')
endif

thread_dep = dependency('threads')
platform_c_args = platform_defs

if get_option('tool_script')
    platform_c_args += '-DCONFIG_RTCLAW_TOOL_SCRIPT'
endif

rtclaw_exe = executable('rtclaw',
    platform_src,
    include_directories: [claw_inc, claw_root_inc],
    c_args: platform_c_args,
    dependencies: [rtclaw_dep, osal_dep, cjson_dep, thread_dep],
    install: false,
)

# Unit test executable — links ota_stub.c for OTA symbol resolution
test_main_src = files('../../tests/unit/test_main_linux.c')
test_incs = include_directories('../../tests/unit')
test_platform_src = files('ota_stub.c')

if get_option('tool_script')
    test_platform_src += files('scripting.c')
endif

rtclaw_test_exe = executable('rtclaw_test',
    test_main_src + test_platform_src,
    include_directories: [claw_inc, claw_root_inc, test_incs],
    c_args: platform_c_args,
    link_with: [unittest_lib],
    dependencies: [rtclaw_dep, osal_dep, cjson_dep, thread_dep],
    install: false,
)
