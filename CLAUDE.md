# CLAUDE.md

rt-claw: OpenClaw-inspired AI assistant on embedded RTOS (FreeRTOS + RT-Thread) via OSAL.

## Build

Meson cross-compiles `src/` and `osal/` into static libraries, then each platform's
native build system (SCons/CMake) links them into the final firmware.
All outputs go to `build/<platform>/`.

```bash
# Unified entry (from project root)
make qemu-a9                           # Meson + SCons → build/qemu-a9/
make esp32c3                           # Meson + idf.py (requires ESP-IDF)
make esp32s3                           # Meson + idf.py (requires ESP-IDF)

# Meson only (libraries)
meson setup build/qemu-a9 --cross-file platform/qemu-a9-rtthread/cross.ini
meson compile -C build/qemu-a9

# ESP32-C3: auto-generated cross.ini from ESP-IDF config
python3 scripts/gen-esp32c3-cross.py
meson setup build/esp32c3 --cross-file platform/esp32c3/cross.ini
meson compile -C build/esp32c3

# ESP32-S3: auto-generated cross.ini from ESP-IDF config
python3 scripts/gen-esp32s3-cross.py
meson setup build/esp32s3 --cross-file platform/esp32s3/cross.ini
meson compile -C build/esp32s3
```

## Run

```bash
# QEMU vexpress-a9 (RT-Thread)
make run-qemu-a9                       # build + launch QEMU
tools/qemu-run.sh -M qemu-a9           # launch only (must build first)
tools/qemu-run.sh -M qemu-a9 -g        # debug mode (GDB port 1234)

# ESP32-C3 QEMU (requires ESP-IDF)
make run-esp32c3                       # build + launch QEMU
tools/qemu-run.sh -M esp32c3           # launch only
tools/qemu-run.sh -M esp32c3 --graphics  # with LCD display window
tools/qemu-run.sh -M esp32c3 -g        # debug mode (GDB port 1234)

# ESP32-S3 QEMU (requires ESP-IDF)
make run-esp32s3                       # build + launch QEMU
tools/qemu-run.sh -M esp32s3           # launch only
tools/qemu-run.sh -M esp32s3 --graphics  # with LCD display window
tools/qemu-run.sh -M esp32s3 -g        # debug mode (GDB port 1234)

# Shell completion
eval "$(tools/qemu-run.sh --setup-completion)"
```

## Code Style

- C99 (gnu99), 4-space indent, ~80 char line width (max 100)
- Naming: `snake_case` for variables/functions, `CamelCase` for structs/enums, `UPPER_CASE` for constants/enum values
- Public API prefix: `claw_` (OSAL), subsystem prefix for services (e.g. `gateway_`)
- Comments: `/* C style only */`, no `//`
- Header guards: `CLAW_<PATH>_<NAME>_H`
- Include order (src/): `claw_os.h` -> system headers -> project headers
- Always use braces for control flow blocks
- License header on every source file: `SPDX-License-Identifier: MIT`

Full reference: [docs/en/coding-style.md](docs/en/coding-style.md)

## Commit Convention

Format: `subsystem: short description` (max 76 chars), body wrapped at 76 chars.

Every commit **must** include `Signed-off-by` (`git commit -s`).

Subsystem prefixes: `osal`, `gateway`, `swarm`, `net`, `ai`, `platform`, `build`, `docs`, `tools`, `scripts`, `main`.

## Checks

```bash
# Style check (src/ and osal/ only)
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
3. QEMU boot test: `tools/qemu-run.sh -M qemu-a9` or `tools/qemu-run.sh -M esp32c3` or `tools/qemu-run.sh -M esp32s3`

## Key Paths

| Path | Purpose |
|------|---------|
| `Makefile` | Unified build entry point |
| `meson.build` | Root Meson project (cross-compiles src/ + osal/) |
| `meson.options` | Build options (osal backend, feature flags) |
| `build/<platform>/` | Build outputs (gitignored) |
| `osal/include/claw_os.h` | OSAL API (the only header core code includes) |
| `osal/freertos/` | FreeRTOS OSAL implementation |
| `osal/rtthread/` | RT-Thread OSAL implementation |
| `src/claw_init.c` | Boot entry point |
| `src/claw_config.h` | Compile-time constants (platform-independent) |
| `src/core/gateway.*` | Message router |
| `src/services/{ai,net,swarm}/` | Service modules |
| `src/tools/` | Tool Use framework |
| `platform/esp32c3/` | ESP32-C3 ESP-IDF project + auto-gen cross-file |
| `platform/esp32s3/` | ESP32-S3 ESP-IDF project + auto-gen cross-file |
| `platform/qemu-a9-rtthread/` | RT-Thread BSP + Meson cross-file |
| `scripts/gen-esp32c3-cross.py` | Generate ESP32-C3 Meson cross-file |
| `scripts/gen-esp32s3-cross.py` | Generate ESP32-S3 Meson cross-file |
| `tools/qemu-run.sh` | Unified QEMU launcher (-M qemu-a9/esp32c3/esp32s3) |
| `tools/api-proxy.py` | HTTP→HTTPS proxy for QEMU without TLS |
