# CLAUDE.md

rt-claw: OpenClaw-inspired AI assistant on embedded RTOS (FreeRTOS + RT-Thread) via OSAL.

## Build

Meson cross-compiles `claw/` and `osal/` into static libraries, then each platform's
native build system (SCons/CMake) links them into the final firmware.
All outputs go to `build/<platform>/`.

```bash
# Unified entry (from project root)
make build-esp32c3-qemu                # ESP32-C3 QEMU (default)
make build-esp32c3-xiaozhi-xmini             # ESP32-C3 xiaozhi-xmini 16MB board
make build-esp32c3-devkit              # ESP32-C3 generic 4MB devkit
make vexpress-a9-qemu                  # Meson + SCons → build/vexpress-a9-qemu/
make build-esp32s3-qemu                # Meson + idf.py (requires ESP-IDF)
make esp32s3                           # Meson + idf.py (real hardware)

# Meson only (libraries)
meson setup build/vexpress-a9-qemu --cross-file platform/vexpress-a9/cross.ini
meson compile -C build/vexpress-a9-qemu

# ESP32-C3: auto-generated cross.ini from ESP-IDF config
python3 scripts/gen-esp32c3-cross.py qemu
meson setup build/esp32c3-qemu/meson --cross-file build/esp32c3-qemu/cross.ini
meson compile -C build/esp32c3-qemu/meson

# ESP32-S3: auto-generated cross.ini from ESP-IDF config
python3 scripts/gen-esp32s3-cross.py qemu
meson setup build/esp32s3-qemu/meson --cross-file build/esp32s3-qemu/cross.ini
meson compile -C build/esp32s3-qemu/meson
```

## Configuration

All rt-claw business config (AI, Feishu, tuning) lives in `claw_config.h` (project root).
Credentials are set via Meson or environment variables — NOT in vendor SDK configs.

```bash
# Environment variables (read at Meson configure time)
export RTCLAW_AI_API_KEY='sk-...'         # AI API key
export RTCLAW_AI_API_URL='https://...'    # API endpoint
export RTCLAW_AI_MODEL='claude-sonnet-4-6'  # Model name
export RTCLAW_FEISHU_APP_ID='cli_...'     # Feishu App ID
export RTCLAW_FEISHU_APP_SECRET='...'     # Feishu App Secret

# Or Meson options
meson configure build/<platform>/meson -Dai_api_key='sk-...'
```

Priority: meson option (`-Dai_api_key=...`) > env var (`RTCLAW_AI_API_KEY`) > `claw_config.h` default.

Meson generates `claw_gen_config.h` in the build directory with the resolved values.

## Run

```bash
# QEMU vexpress-a9 (RT-Thread)
make run-vexpress-a9-qemu              # build + launch QEMU
make run-vexpress-a9-qemu GDB=1       # debug mode (GDB port 1234)

# ESP32-C3 (board = qemu | devkit | xiaozhi-xmini)
make run-esp32c3-qemu                 # build + launch QEMU simulator
make run-esp32c3-qemu GDB=1          # debug mode (GDB port 1234)
make run-esp32c3-qemu GRAPHICS=1     # with LCD display window
make flash-esp32c3-xiaozhi-xmini            # build + flash xiaozhi-xmini
make run-esp32c3-xiaozhi-xmini              # serial monitor (hardware)

# ESP32-S3 (board = qemu | default)
make run-esp32s3-qemu                  # build + launch QEMU
make run-esp32s3-qemu GRAPHICS=1      # with LCD display window
make run-esp32s3-qemu GDB=1           # debug mode (GDB port 1234)

# ESP32-S3 real hardware
make flash-esp32s3                     # build + flash firmware
make monitor-esp32s3                   # serial monitor
```

## Code Style

- C99 (gnu99), 4-space indent, ~80 char line width (max 100)
- Naming: `snake_case` for variables/functions, `CamelCase` for structs/enums, `UPPER_CASE` for constants/enum values
- Public API prefix: `claw_` (OSAL), subsystem prefix for services (e.g. `gateway_`)
- Comments: `/* C style only */`, no `//`
- Header guards: `CLAW_<PATH>_<NAME>_H`
- Include order (claw/): `claw_os.h` -> system headers -> project headers
- Always use braces for control flow blocks
- License header on every source file: `SPDX-License-Identifier: MIT`

Full reference: [docs/en/coding-style.md](docs/en/coding-style.md)

## Commit Convention

Format: `subsystem: short description` (max 76 chars), body wrapped at 76 chars.

Every commit **must** include `Signed-off-by` (`git commit -s`).

Subsystem prefixes: `osal`, `gateway`, `swarm`, `net`, `ai`, `platform`, `build`, `docs`, `tools`, `scripts`, `main`, `drivers`, `sched`, `feishu`, `ci`, `claw`, `tests`.

## Checks

```bash
# Style check (claw/, osal/, include/ only)
scripts/check-patch.sh                 # all source files
scripts/check-patch.sh --staged        # staged changes only
scripts/check-patch.sh --file <path>   # specific file

# DCO (Signed-off-by) check
scripts/check-dco.sh                   # commits since main
scripts/check-dco.sh --last 3          # last 3 commits

# Install git hooks (pre-commit + commit-msg)
scripts/install-hooks.sh
```

## Testing

No unit test framework yet. Verify changes by:

1. Build passes on at least one platform
2. `scripts/check-patch.sh --staged` passes
3. QEMU boot test: `make run-vexpress-a9-qemu` or `make run-esp32c3-qemu` or `make run-esp32s3-qemu`

## Key Paths

| Path | Purpose |
|------|---------|
| `Makefile` | Unified build entry point |
| `meson.build` | Root Meson project (cross-compiles claw/ + osal/) |
| `meson_options.txt` | Build options (osal backend, feature flags, AI/Feishu config) |
| `build/<platform>/` | Build outputs (gitignored) |
| `include/` | Unified public headers (claw_os.h, claw_net.h, claw_board.h, etc.) |
| `include/drivers/` | Driver public headers (mirror of drivers/ structure) |
| `include/utils/` | Utility headers (bitops.h, list.h) |
| `drivers/net/espressif/` | Espressif WiFi driver (shared across C3/S3) |
| `drivers/audio/espressif/` | ES8311 audio codec driver |
| `drivers/display/espressif/` | SSD1306 OLED display driver |
| `drivers/serial/espressif/` | Serial console driver |
| `osal/freertos/` | FreeRTOS OSAL implementation |
| `osal/rtthread/` | RT-Thread OSAL implementation |
| `vendor/lib/cjson/` | cJSON library |
| `vendor/os/freertos/` | FreeRTOS-Kernel (submodule) |
| `vendor/os/rt-thread/` | RT-Thread (submodule) |
| `claw/claw_init.c` | Boot entry point |
| `claw_config.h` | Unified compile-time configuration (project root) |
| `claw_gen_config.h.in` | Meson template for generated config header |
| `claw/core/gateway.c` | Message router |
| `claw/services/{ai,net,swarm,im}/` | Service modules |
| `claw/tools/` | Tool Use framework |
| `platform/common/espressif/` | Shared Espressif board helpers (WiFi init + shell) |
| `platform/esp32c3/` | ESP32-C3 unified ESP-IDF project (all boards) |
| `platform/esp32c3/boards/` | Board-specific configs (qemu, devkit, xiaozhi-xmini) |
| `platform/esp32s3/` | ESP32-S3 unified ESP-IDF project (all boards) |
| `platform/esp32s3/boards/` | Board-specific configs (qemu, default) |
| `platform/vexpress-a9/` | RT-Thread BSP + Meson cross-file |
| `scripts/gen-esp32c3-cross.py` | Generate ESP32-C3 Meson cross-file |
| `scripts/gen-esp32s3-cross.py` | Generate ESP32-S3 Meson cross-file |
| `scripts/api-proxy.py` | HTTP→HTTPS proxy for QEMU without TLS |
