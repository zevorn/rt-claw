#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# PlatformIO post-configure script for rt-claw.
#
# Runs AFTER ESP-IDF cmake configure (so compile_commands.json exists).
# Invokes the Meson bridge helper, then injects Meson objects into
# the firmware link via env.Append(LINKFLAGS=...).

import glob
import os
import subprocess
import sys

Import("env")  # noqa: F821  PlatformIO SCons global


def _fail(msg):
    sys.stderr.write(f"[pio_post_configure] Error: {msg}\n")
    env.Exit(1)


def _info(msg):
    print(f"[pio_post_configure] {msg}")


build_targets = COMMAND_LINE_TARGETS or ["buildprog"]  # noqa: F821
skip_targets = {"clean", "cleanall", "idedata", "menuconfig"}
if any(t in skip_targets for t in build_targets):
    pass
else:
    board = env.GetProjectOption("custom_rtclaw_board", None)
    if not board:
        _fail("custom_rtclaw_board not set")

    pio_board = env.BoardConfig()
    mcu = pio_board.get("build.mcu", "esp32c3")
    chip = "esp32s3" if ("esp32s3" in mcu or "esp32-s3" in mcu) else "esp32c3"

    project_dir = env.subst("$PROJECT_DIR")
    project_root = os.path.dirname(os.path.dirname(project_dir))
    build_dir = env.subst("$BUILD_DIR")
    board_build_dir = os.path.join(project_root, "build",
                                   f"{chip}-{board}")
    meson_build_dir = os.path.join(board_build_dir, "meson")

    cc_json = os.path.join(build_dir, "compile_commands.json")
    if not os.path.exists(cc_json):
        _fail(f"compile_commands.json not found at {cc_json}")

    bridge = os.path.join(project_root, "scripts", "pio_meson_bridge.py")

    platform_obj = env.PioPlatform()
    framework_dir = platform_obj.get_package_dir("framework-espidf") or ""

    _info(f"Running Meson bridge for {chip}-{board}...")
    rc = subprocess.call(
        [sys.executable, bridge,
         "--chip", chip,
         "--board", board,
         "--idf-build-dir", build_dir,
         "--board-build-dir", board_build_dir,
         "--meson-build-dir", meson_build_dir,
         "--framework-path", framework_dir],
        cwd=project_root)
    if rc != 0:
        _fail("pio_meson_bridge.py failed")

    meson_objs = []
    for subdir in ["claw/librtclaw.a.p",
                    "osal/libosal.a.p",
                    "vendor/lib/cjson/libcjson.a.p"]:
        d = os.path.join(meson_build_dir, subdir)
        if os.path.isdir(d):
            meson_objs.extend(
                os.path.join(d, f) for f in os.listdir(d)
                if f.endswith(".o")
            )
    if not meson_objs:
        _fail(f"No Meson objects in {meson_build_dir}")

    env.Append(
        LINKFLAGS=["-Wl,--whole-archive"] +
                  meson_objs +
                  ["-Wl,--no-whole-archive"]
    )
    _info(f"Injected {len(meson_objs)} Meson objects into firmware link")
