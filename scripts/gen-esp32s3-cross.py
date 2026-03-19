#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# Generate Meson cross-file for ESP32-S3 from ESP-IDF build config.
#
# Prerequisites:
#   cd platform/esp32s3 && idf.py -B <build-dir> -DRTCLAW_BOARD=<board> set-target esp32s3
#
# Usage:
#   python3 scripts/gen-esp32s3-cross.py [board]
#
# board defaults to "qemu".
# Examples:
#   python3 scripts/gen-esp32s3-cross.py              # QEMU
#   python3 scripts/gen-esp32s3-cross.py default       # real hardware

import json
import os
import re
import sys

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BOARD = sys.argv[1] if len(sys.argv) > 1 else 'qemu'

BOARD_BUILD_DIR = os.path.join(PROJECT_ROOT, 'build', f'esp32s3-{BOARD}')
IDF_BUILD_DIR = os.path.join(BOARD_BUILD_DIR, 'idf')
CC_JSON = os.path.join(IDF_BUILD_DIR, 'compile_commands.json')
CROSS_INI = os.path.join(BOARD_BUILD_DIR, 'cross.ini')
SDKCONFIG_H = os.path.join(IDF_BUILD_DIR, 'config', 'sdkconfig.h')


def main():
    if not os.path.exists(CC_JSON):
        print(f'Error: {CC_JSON} not found.', file=sys.stderr)
        print('Run first:', file=sys.stderr)
        print(f'  cd platform/esp32s3 && idf.py -B {IDF_BUILD_DIR} '
              f'-DRTCLAW_BOARD={BOARD} set-target esp32s3',
              file=sys.stderr)
        return 1

    with open(CC_JSON) as f:
        cc = json.load(f)

    """ Find a source entry to extract ESP-IDF flags """
    entry = None
    search_order = ['rt_claw_stub.c', 'claw/claw_init.c', 'main/main.c']
    for pattern in search_order:
        for e in cc:
            if pattern in e.get('file', ''):
                entry = e
                break
        if entry:
            break

    if not entry:
        print('Error: no suitable source found in compile_commands.json',
              file=sys.stderr)
        return 1

    cmd = entry['command']

    """ Extract compiler path """
    compiler = cmd.split()[0]
    ar = compiler.replace('-gcc', '-ar')
    strip_bin = compiler.replace('-gcc', '-strip')

    """ Extract include paths (ESP-IDF / managed_components / build config) """
    all_includes = re.findall(r'-I(\S+)', cmd)
    all_includes += re.findall(r'-isystem\s+(\S+)', cmd)
    project_src_dirs = {'rt-claw/src', 'rt-claw/osal', 'rt-claw/vendor'}
    idf_includes = []
    for p in all_includes:
        if any(d in p for d in project_src_dirs):
            continue
        idf_includes.append(p)

    """ Architecture and compiler flags (Xtensa LX7) """
    c_args = [
        '-mlongcalls',
        '-ffunction-sections',
        '-fdata-sections',
        '-fstrict-volatile-bitfields',
        '-fdiagnostics-color=always',
    ]

    """ Force-include sdkconfig.h so CONFIG_* macros are available """
    if os.path.exists(SDKCONFIG_H):
        c_args.append(f'-include')
        c_args.append(SDKCONFIG_H)

    """ ESP-IDF platform defines """
    c_args.extend([
        '-DCLAW_PLATFORM_ESP_IDF',
        '-DESP_PLATFORM',
        '-D_GNU_SOURCE',
        '-D_POSIX_READER_WRITER_LOCKS',
    ])

    # AI/Feishu credentials are now managed by Meson (claw_config_generated.h).
    # No env_override.h needed — Meson reads env vars and generates the header.

    """ Add ESP-IDF include paths as system includes """
    for inc in idf_includes:
        c_args.append(f'-isystem')
        c_args.append(inc)

    """ Write cross.ini """
    os.makedirs(os.path.dirname(CROSS_INI), exist_ok=True)
    with open(CROSS_INI, 'w') as f:
        f.write('# Auto-generated Meson cross-file for ESP32-S3 (ESP-IDF)\n')
        f.write(f'# Board: {BOARD}\n')
        f.write('# Regenerate: python3 scripts/gen-esp32s3-cross.py\n')
        f.write('# DO NOT edit manually or commit to git.\n\n')

        f.write('[binaries]\n')
        f.write(f"c = '{compiler}'\n")
        f.write(f"ar = '{ar}'\n")
        f.write(f"strip = '{strip_bin}'\n\n")

        f.write('[host_machine]\n')
        f.write("system = 'none'\n")
        f.write("cpu_family = 'xtensa'\n")
        f.write("cpu = 'esp32s3'\n")
        f.write("endian = 'little'\n\n")

        f.write('[built-in options]\n')
        args_str = ', '.join(f"'{a}'" for a in c_args)
        f.write(f'c_args = [{args_str}]\n')
        f.write("b_staticpic = false\n")
        f.write("b_pie = false\n\n")

        f.write('[project options]\n')
        f.write("osal = 'freertos'\n")
        """
        Feature flags disabled here — they come from sdkconfig.h
        via the -include flag above.
        """
        f.write('swarm = false\n')
        f.write('sched = false\n')
        f.write('skill = false\n')
        f.write('tool_gpio = false\n')
        f.write('tool_system = false\n')
        f.write('tool_sched = false\n')

    print(f'Generated: {CROSS_INI}')
    print(f'  Board:     {BOARD}')
    print(f'  Compiler:  {compiler}')
    print(f'  Includes:  {len(idf_includes)} ESP-IDF paths')
    print(f'  sdkconfig: {SDKCONFIG_H}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
