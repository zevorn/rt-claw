#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Local OTA server for rt-claw development.

Serves a version.json and firmware binary so the device can OTA update
over the local network.

Usage:
    scripts/ota-server.py                          # auto-detect from build/
    scripts/ota-server.py --platform esp32c3 --board xiaozhi-xmini
    scripts/ota-server.py --firmware path/to/rt-claw.bin --version 0.2.0
    scripts/ota-server.py --port 9000 --bind 0.0.0.0
"""

import argparse
import hashlib
import http.server
import json
import os
import re
import socket
import sys
import tempfile
import shutil


PROJECT_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..")
)


def get_local_ip():
    """Best-effort local IP detection."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"


def read_version_from_config():
    """Parse RT_CLAW_VERSION from claw_config.h."""
    config_path = os.path.join(PROJECT_ROOT, "claw_config.h")
    if not os.path.isfile(config_path):
        return "0.0.0"
    with open(config_path) as f:
        for line in f:
            m = re.match(
                r'#define\s+RT_CLAW_VERSION\s+"([^"]+)"', line
            )
            if m:
                return m.group(1)
    return "0.0.0"


def find_firmware(platform, board):
    """Auto-detect firmware binary from build directory."""
    build_dir = os.path.join(
        PROJECT_ROOT, "build", f"{platform}-{board}", "idf"
    )
    for name in ("rt-claw.bin", "firmware.bin"):
        path = os.path.join(build_dir, name)
        if os.path.isfile(path):
            return path
    return None


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            chunk = f.read(65536)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser(
        description="Local OTA server for rt-claw"
    )
    parser.add_argument(
        "--platform", default="esp32c3",
        help="Platform (default: esp32c3)",
    )
    parser.add_argument(
        "--board", default="xiaozhi-xmini",
        help="Board name (default: xiaozhi-xmini)",
    )
    parser.add_argument(
        "--firmware",
        help="Path to firmware .bin (auto-detect if omitted)",
    )
    parser.add_argument(
        "--version",
        help="Version string (auto-detect from claw_config.h if omitted)",
    )
    parser.add_argument(
        "--port", type=int, default=8080,
        help="HTTP port (default: 8080)",
    )
    parser.add_argument(
        "--bind", default="0.0.0.0",
        help="Bind address (default: 0.0.0.0)",
    )
    args = parser.parse_args()

    # Locate firmware
    fw_path = args.firmware
    if not fw_path:
        fw_path = find_firmware(args.platform, args.board)
    if not fw_path or not os.path.isfile(fw_path):
        print(
            f"Firmware not found. Build first:\n"
            f"  make build-{args.platform}-{args.board}\n"
            f"Or specify --firmware <path>",
            file=sys.stderr,
        )
        return 1

    version = args.version or read_version_from_config()
    fw_size = os.path.getsize(fw_path)
    fw_sha256 = sha256_file(fw_path)
    local_ip = get_local_ip()
    fw_filename = "rt-claw.bin"

    # Create serve directory
    serve_dir = tempfile.mkdtemp(prefix="rtclaw-ota-")
    shutil.copy2(fw_path, os.path.join(serve_dir, fw_filename))

    fw_url = f"http://{local_ip}:{args.port}/{fw_filename}"
    version_json = {
        "version": version,
        "url": fw_url,
        "size": fw_size,
        "sha256": fw_sha256,
    }
    with open(os.path.join(serve_dir, "version.json"), "w") as f:
        json.dump(version_json, f, indent=4)

    version_url = f"http://{local_ip}:{args.port}/version.json"

    print()
    print("  ┌─────────────────────────────────────────────┐")
    print("  │          rt-claw OTA Server                  │")
    print("  └─────────────────────────────────────────────┘")
    print()
    print(f"  Firmware:  {fw_path}")
    print(f"  Version:   {version}")
    print(f"  Size:      {fw_size} bytes")
    print(f"  SHA256:    {fw_sha256[:16]}...")
    print()
    print(f"  Version URL:  {version_url}")
    print(f"  Firmware URL: {fw_url}")
    print()
    print("  ── Device Configuration ──")
    print()
    print(f"  Meson:   -Dota_url='{version_url}'")
    print(f"  Env:     RTCLAW_OTA_URL='{version_url}'")
    print()
    print("  ── Shell Commands ──")
    print()
    print(f"  /ota check                     # check for update")
    print(f"  /ota update                    # check + install")
    print(f"  /ota update {fw_url}")
    print(f"                                 # direct URL install")
    print()
    print(f"  Serving on {args.bind}:{args.port} ...")
    print(f"  Press Ctrl+C to stop.")
    print()

    os.chdir(serve_dir)
    handler = http.server.SimpleHTTPRequestHandler
    try:
        with http.server.HTTPServer((args.bind, args.port), handler) as srv:
            srv.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        shutil.rmtree(serve_dir, ignore_errors=True)

    return 0


if __name__ == "__main__":
    sys.exit(main())
