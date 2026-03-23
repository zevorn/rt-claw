# SPDX-License-Identifier: MIT
# Linux native platform: link claw + osal + cjson → executable

platform_src = files(
    'main.c',
    'shell.c',
    'board.c',
    'ota_stub.c',
)

thread_dep = dependency('threads')

rtclaw_exe = executable('rtclaw',
    platform_src,
    include_directories: [claw_inc, claw_root_inc],
    c_args: platform_defs,
    dependencies: [rtclaw_dep, osal_dep, cjson_dep, thread_dep],
    install: false,
)

# Unit test executable — links ota_stub.c for OTA symbol resolution
test_main_src = files('../../tests/unit/test_main_linux.c')
test_incs = include_directories('../../tests/unit')

rtclaw_test_exe = executable('rtclaw_test',
    test_main_src + files('ota_stub.c'),
    include_directories: [claw_inc, claw_root_inc, test_incs],
    c_args: platform_defs,
    link_with: [unittest_lib],
    dependencies: [rtclaw_dep, osal_dep, cjson_dep, thread_dep],
    install: false,
)
