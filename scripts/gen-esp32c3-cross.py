#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# Generate Meson cross-file for ESP32-C3 from ESP-IDF build config.
#
# Prerequisites:
#   cd platform/esp32c3 && idf.py -B <build-dir> -DRTCLAW_BOARD=<board> set-target esp32c3
#
# Usage:
#   python3 scripts/gen-esp32c3-cross.py [board]
#
# board defaults to "qemu".
# Examples:
#   python3 scripts/gen-esp32c3-cross.py              # QEMU
#   python3 scripts/gen-esp32c3-cross.py devkit        # devkit board
#   python3 scripts/gen-esp32c3-cross.py xiaozhi-xmini  # xiaozhi-xmini board

import argparse
import io
import json
import os
import re
import sys

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def parse_args():
    parser = argparse.ArgumentParser(
        description='Generate Meson cross-file for ESP32-C3 from ESP-IDF '
                    'build config.')
    parser.add_argument('board', nargs='?', default='qemu',
                        help='Board name (default: qemu)')
    parser.add_argument('--idf-build-dir',
                        help='Override IDF build directory path')
    parser.add_argument('--board-build-dir',
                        help='Override board build directory path')
    parser.add_argument('--cross-file',
                        help='Override output cross-file path')
    return parser.parse_args()


def main():
    args = parse_args()
    board = args.board

    board_build_dir = (args.board_build_dir or
                       os.path.join(PROJECT_ROOT, 'build',
                                    f'esp32c3-{board}'))
    idf_build_dir = (args.idf_build_dir or
                     os.path.join(board_build_dir, 'idf'))
    cc_json = os.path.join(idf_build_dir, 'compile_commands.json')
    cross_ini = (args.cross_file or
                 os.path.join(board_build_dir, 'cross.ini'))
    sdkconfig_h = os.path.join(idf_build_dir, 'config', 'sdkconfig.h')

    if not os.path.exists(cc_json):
        print(f'Error: {cc_json} not found.', file=sys.stderr)
        print('Run first:', file=sys.stderr)
        print(f'  cd platform/esp32c3 && idf.py -B {idf_build_dir} '
              f'-DRTCLAW_BOARD={board} set-target esp32c3',
              file=sys.stderr)
        return 1

    with open(cc_json) as f:
        cc = json.load(f)

    """ Find a source entry to extract ESP-IDF flags """
    entry = None
    search_order = ['rt_claw_stub.c', 'claw/init.c', 'main/main.c']
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
        if not os.path.isabs(p):
            p = os.path.normpath(os.path.join(idf_build_dir, p))
        idf_includes.append(p)

    """ Architecture and compiler flags """
    c_args = [
        '-march=rv32imc_zicsr_zifencei',
        '-ffunction-sections',
        '-fdata-sections',
        '-fno-jump-tables',
        '-fno-tree-switch-conversion',
        '-fdiagnostics-color=always',
    ]

    """ Force-include sdkconfig.h so CONFIG_* macros are available """
    if os.path.exists(sdkconfig_h):
        c_args.append(f'-include')
        c_args.append(sdkconfig_h)

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

    """ Write cross.ini (only update file if content changed) """
    os.makedirs(os.path.dirname(cross_ini), exist_ok=True)
    buf = io.StringIO()
    f = buf

    f.write('# Auto-generated Meson cross-file for ESP32-C3 (ESP-IDF)\n')
    f.write(f'# Board: {board}\n')
    f.write('# Regenerate: python3 scripts/gen-esp32c3-cross.py\n')
    f.write('# DO NOT edit manually or commit to git.\n\n')

    f.write('[binaries]\n')
    f.write(f"c = '{compiler}'\n")
    f.write(f"ar = '{ar}'\n")
    f.write(f"strip = '{strip_bin}'\n\n")

    f.write('[host_machine]\n')
    f.write("system = 'none'\n")
    f.write("cpu_family = 'riscv32'\n")
    f.write("cpu = 'esp32c3'\n")
    f.write("endian = 'little'\n\n")

    f.write('[built-in options]\n')
    args_str = ', '.join(f"'{a}'" for a in c_args)
    f.write(f'c_args = [{args_str}]\n')
    f.write("b_staticpic = false\n")
    f.write("b_pie = false\n\n")

    f.write('[project options]\n')
    f.write("osal = 'freertos'\n")
    # Sync Meson feature flags from sdkconfig.h
    kconfig_to_meson = {
        'CONFIG_RTCLAW_SWARM_ENABLE':     'swarm',
        'CONFIG_RTCLAW_SCHED_ENABLE':     'sched',
        'CONFIG_RTCLAW_SKILL_ENABLE':     'skill',
        'CONFIG_RTCLAW_TOOL_GPIO':        'tool_gpio',
        'CONFIG_RTCLAW_TOOL_SYSTEM':      'tool_system',
        'CONFIG_RTCLAW_TOOL_SCHED':       'tool_sched',
        'CONFIG_RTCLAW_TOOL_NET':         'tool_net',
        'CONFIG_RTCLAW_TOOL_MOUSE':       'tool_mouse',
        'CONFIG_RTCLAW_HEARTBEAT_ENABLE': 'heartbeat',
        'CONFIG_RTCLAW_FEISHU_ENABLE':    'feishu',
        'CONFIG_RTCLAW_TELEGRAM_ENABLE':  'telegram',
        'CONFIG_RTCLAW_OTA_ENABLE':       'ota',
    }
    enabled = set()
    if os.path.exists(sdkconfig_h):
        with open(sdkconfig_h) as sh:
            for line in sh:
                for kconf in kconfig_to_meson:
                    if f'#define {kconf} ' in line or \
                       f'#define {kconf}\n' in line:
                        enabled.add(kconf)
    for kconf, mopt in kconfig_to_meson.items():
        val = 'true' if kconf in enabled else 'false'
        f.write(f'{mopt} = {val}\n')

    new_content = buf.getvalue()
    old_content = ''
    if os.path.exists(cross_ini):
        with open(cross_ini) as existing:
            old_content = existing.read()
    if new_content != old_content:
        with open(cross_ini, 'w') as out:
            out.write(new_content)

    print(f'Generated: {cross_ini}')
    print(f'  Board:     {board}')
    print(f'  Compiler:  {compiler}')
    print(f'  Includes:  {len(idf_includes)} ESP-IDF paths')
    print(f'  sdkconfig: {sdkconfig_h}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
