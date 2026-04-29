#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# PlatformIO pre-build script for rt-claw.
#
# Orchestrates the existing Meson + ESP-IDF build chain so that
# pio run produces a flashable firmware without manual toolchain
# setup.  Runs as a pre:extra_script in platformio.ini.

import hashlib
import os
import shutil
import subprocess
import sys

Import("env")  # noqa: F821  PlatformIO SCons global


def _fail(msg):
    sys.stderr.write(f"[pio_pre_build] Error: {msg}\n")
    env.Exit(1)


def _info(msg):
    print(f"[pio_pre_build] {msg}")


def _check_tool(name):
    if shutil.which(name) is None:
        _fail(f"'{name}' not found. Install it:\n"
              f"  pip install {name}" if name == "meson" else
              f"  apt install ninja-build  (or pip install ninja)")


def _resolve_idf_path():
    """Return the ESP-IDF root from PlatformIO's framework-espidf package."""
    platform = env.PioPlatform()
    framework_dir = platform.get_package_dir("framework-espidf")
    if not framework_dir or not os.path.isdir(framework_dir):
        _fail("framework-espidf package not found in PlatformIO")
    return framework_dir


def _hash_files(paths):
    """Compute a combined sha256 over a list of file paths."""
    h = hashlib.sha256()
    for p in sorted(paths):
        if os.path.exists(p):
            with open(p, "rb") as f:
                h.update(f.read())
    return h.hexdigest()


def _stamp_changed(stamp_path, current_hash):
    if not os.path.exists(stamp_path):
        return True
    with open(stamp_path) as f:
        return f.read().strip() != current_hash


def _write_stamp(stamp_path, current_hash):
    os.makedirs(os.path.dirname(stamp_path), exist_ok=True)
    with open(stamp_path, "w") as f:
        f.write(current_hash)


def pre_build(source, target, env):
    build_type = env.GetBuildType()
    if build_type in ("clean", "cleanall"):
        _info("Skipping Meson chain for clean target")
        return

    _check_tool("meson")
    _check_tool("ninja")

    board = env.GetProjectOption("custom_rtclaw_board", None)
    if not board:
        _fail("custom_rtclaw_board not set in platformio.ini environment")

    pio_board = env.BoardConfig()
    mcu = pio_board.get("build.mcu", "esp32c3")
    if "esp32s3" in mcu or "esp32-s3" in mcu:
        chip = "esp32s3"
    else:
        chip = "esp32c3"

    idf_path = _resolve_idf_path()
    idf_py = os.path.join(idf_path, "tools", "idf.py")
    if not os.path.exists(idf_py):
        _fail(f"idf.py not found at {idf_py}")

    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    board_build_dir = os.path.join(project_root, "build",
                                   f"{chip}-{board}")
    idf_build_dir = os.path.join(board_build_dir, "idf")
    meson_build_dir = os.path.join(board_build_dir, "meson")

    platform_dir = os.path.join(project_root, "platform", chip)
    sdkconfig_defaults = os.path.join(platform_dir, "boards", board,
                                      "sdkconfig.defaults")

    stamp_path = os.path.join(board_build_dir, ".pio_stamp")
    hash_inputs = [
        sdkconfig_defaults,
        os.path.join(project_root, "meson.build"),
        os.path.join(project_root, "meson_options.txt"),
        os.path.join(project_root, "claw_config.h"),
    ]
    current_hash = _hash_files(hash_inputs)
    needs_reconfigure = _stamp_changed(stamp_path, current_hash)

    cc_json = os.path.join(idf_build_dir, "compile_commands.json")
    idf_env = os.environ.copy()
    idf_env["IDF_PATH"] = idf_path

    if not os.path.exists(cc_json) or needs_reconfigure:
        _info(f"Configuring IDF for {chip}-{board}...")
        subprocess.check_call(
            [sys.executable, idf_py,
             "-B", idf_build_dir,
             f"-DRTCLAW_BOARD={board}",
             "set-target", chip],
            cwd=platform_dir, env=idf_env)
        subprocess.check_call(
            [sys.executable, idf_py,
             "-B", idf_build_dir,
             f"-DRTCLAW_BOARD={board}",
             "reconfigure"],
            cwd=platform_dir, env=idf_env)

    gen_cross = os.path.join(project_root, "scripts",
                             f"gen-{chip}-cross.py")
    _info(f"Generating Meson cross-file for {board}...")
    subprocess.check_call(
        [sys.executable, gen_cross,
         "--idf-build-dir", idf_build_dir,
         "--board-build-dir", board_build_dir,
         board],
        cwd=project_root, env=idf_env)

    cross_ini = os.path.join(board_build_dir, "cross.ini")
    build_ninja = os.path.join(meson_build_dir, "build.ninja")
    if not os.path.exists(build_ninja):
        _info("Running meson setup...")
        subprocess.check_call(
            ["meson", "setup", meson_build_dir,
             f"--cross-file={cross_ini}"],
            cwd=project_root)
    elif needs_reconfigure:
        _info("Running meson setup --reconfigure...")
        subprocess.check_call(
            ["meson", "setup", "--reconfigure", meson_build_dir,
             f"--cross-file={cross_ini}"],
            cwd=project_root)

    _info("Running meson compile...")
    subprocess.check_call(
        ["meson", "compile", "-C", meson_build_dir],
        cwd=project_root)

    _write_stamp(stamp_path, current_hash)

    env.Append(CPPDEFINES=[
        ("RTCLAW_BOARD", f'\\"{board}\\"'),
    ])

    _info(f"Meson build complete for {chip}-{board}")


env.AddPreAction("buildprog", pre_build)
