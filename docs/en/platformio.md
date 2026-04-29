# PlatformIO Build Guide

rt-claw supports PlatformIO as an alternative build system for ESP32-C3 and
ESP32-S3 targets. PlatformIO manages the ESP-IDF toolchain automatically —
no manual installation required.

## Prerequisites

- [PlatformIO Core (CLI)](https://docs.platformio.org/en/latest/core/installation.html)
- [Meson](https://mesonbuild.com/) and [Ninja](https://ninja-build.org/)

```bash
pip install platformio meson ninja
```

## Build

Run from the platform directory (where `platformio.ini` lives):

```bash
# ESP32-C3
cd platform/esp32c3
pio run -e esp32c3_devkit          # generic 4MB devkit
pio run -e esp32c3_xiaozhi_xmini   # xiaozhi-xmini 16MB board
pio run -e esp32c3_qemu            # QEMU virtual board

# ESP32-S3
cd platform/esp32s3
pio run -e esp32s3_default         # real hardware (16MB + PSRAM)
pio run -e esp32s3_qemu            # QEMU virtual board
```

The pre-build script (`scripts/pio_pre_build.py`) automatically:

1. Detects the ESP-IDF bundled with PlatformIO
2. Configures ESP-IDF (`idf.py set-target` + `reconfigure`)
3. Generates Meson cross-compilation files
4. Builds rt-claw libraries via Meson
5. Lets CMake merge the objects into the final firmware

## Flash

```bash
cd platform/esp32c3
pio run -e esp32c3_devkit -t upload
pio run -e esp32c3_xiaozhi_xmini -t upload
```

## Monitor

```bash
pio run -e esp32c3_devkit -t monitor
```

## Environment Variables

API keys and credentials are configured via environment variables (same as
the Makefile build):

```bash
export RTCLAW_AI_API_KEY='sk-...'
export RTCLAW_AI_API_URL='https://...'
export RTCLAW_AI_MODEL='claude-sonnet-4-6'
```

## Coexistence with Makefile

PlatformIO coexists with the existing Makefile/Meson/CMake build. Both
systems share the same source code and build output directories under
`build/<chip>-<board>/`. The Makefile build is unaffected by PlatformIO
files.

## Platform Version

PlatformIO is pinned to `espressif32@6.10.0` (ESP-IDF v5.4.0) to match
the project's toolchain requirements.
