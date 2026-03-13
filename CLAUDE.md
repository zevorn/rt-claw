# CLAUDE.md

rt-claw: OpenClaw-inspired AI assistant on embedded RTOS (FreeRTOS + RT-Thread) via OSAL.

## Build

Meson cross-compiles `claw/` and `osal/` into static libraries, then each platform's
native build system (SCons/CMake) links them into the final firmware.
All outputs go to `build/<platform>/`.

```bash
# Unified entry (from project root)
make vexpress-a9-qemu                           # Meson + SCons → build/vexpress-a9-qemu/
make esp32c3-qemu                      # Meson + idf.py (requires ESP-IDF)
make esp32s3-qemu                      # Meson + idf.py (requires ESP-IDF)

# Meson only (libraries)
meson setup build/vexpress-a9-qemu --cross-file platform/vexpress-a9-qemu/cross.ini
meson compile -C build/vexpress-a9-qemu

# ESP32-C3: auto-generated cross.ini from ESP-IDF config
python3 scripts/gen-esp32c3-cross.py
meson setup build/esp32c3-qemu --cross-file platform/esp32c3-qemu/cross.ini
meson compile -C build/esp32c3-qemu

# ESP32-S3: auto-generated cross.ini from ESP-IDF config
python3 scripts/gen-esp32s3-cross.py
meson setup build/esp32s3-qemu --cross-file platform/esp32s3-qemu/cross.ini
meson compile -C build/esp32s3-qemu
```

## Configuration (env vars)

All platforms support environment variables for credentials (read at build time):

```bash
export RTCLAW_AI_API_KEY='sk-...'         # AI API key
export RTCLAW_AI_API_URL='https://...'    # API endpoint
export RTCLAW_AI_MODEL='claude-sonnet-4-6'  # Model name
export RTCLAW_FEISHU_APP_ID='cli_...'     # Feishu App ID
export RTCLAW_FEISHU_APP_SECRET='...'     # Feishu App Secret
```

Priority: meson option (`-Dai_api_key=...`) > env var > ESP-IDF menuconfig/sdkconfig > `claw_config.h` default.

## Run

```bash
# QEMU vexpress-a9 (RT-Thread)
make run-vexpress-a9-qemu              # build + launch QEMU
make run-vexpress-a9-qemu GDB=1       # debug mode (GDB port 1234)

# ESP32-C3 QEMU (requires ESP-IDF)
make run-esp32c3-qemu                  # build + launch QEMU
make run-esp32c3-qemu GRAPHICS=1      # with LCD display window
make run-esp32c3-qemu GDB=1           # debug mode (GDB port 1234)

# ESP32-S3 QEMU (requires ESP-IDF)
make run-esp32s3-qemu                  # build + launch QEMU
make run-esp32s3-qemu GRAPHICS=1      # with LCD display window
make run-esp32s3-qemu GDB=1           # debug mode (GDB port 1234)
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

Subsystem prefixes: `osal`, `gateway`, `swarm`, `net`, `ai`, `platform`, `build`, `docs`, `tools`, `scripts`, `main`.

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
| `meson_options.txt` | Build options (osal backend, feature flags) |
| `build/<platform>/` | Build outputs (gitignored) |
| `include/` | Unified public headers (claw_os.h, claw_net.h, etc.) |
| `osal/freertos/` | FreeRTOS OSAL implementation |
| `osal/rtthread/` | RT-Thread OSAL implementation |
| `vendor/lib/cjson/` | cJSON library |
| `vendor/os/freertos/` | FreeRTOS-Kernel (submodule) |
| `vendor/os/rt-thread/` | RT-Thread (submodule) |
| `claw/claw_init.c` | Boot entry point |
| `include/claw_config.h` | Compile-time constants (platform-independent) |
| `claw/core/gateway.c` | Message router |
| `claw/services/{ai,net,swarm}/` | Service modules |
| `claw/tools/` | Tool Use framework |
| `platform/esp32c3-qemu/` | ESP32-C3 QEMU ESP-IDF project + auto-gen cross-file |
| `platform/esp32s3-qemu/` | ESP32-S3 QEMU ESP-IDF project + auto-gen cross-file |
| `platform/vexpress-a9-qemu/` | RT-Thread BSP + Meson cross-file |
| `scripts/gen-esp32c3-cross.py` | Generate ESP32-C3 Meson cross-file |
| `scripts/gen-esp32s3-cross.py` | Generate ESP32-S3 Meson cross-file |
| `scripts/api-proxy.py` | HTTP→HTTPS proxy for QEMU without TLS |
