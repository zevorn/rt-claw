# CLAUDE.md

Read `AGENTS.md` first. This file adds Claude Code-specific build, run, test,
and slash-command reference details for rt-claw.

rt-claw: OpenClaw-inspired AI assistant on embedded RTOS (FreeRTOS + RT-Thread + Zephyr) via OSAL.

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
make build-zynq-a9-qemu               # Meson full firmware → build/zynq-a9-qemu/
make build-esp32s3-qemu                # Meson + idf.py (requires ESP-IDF)
make esp32s3                           # Meson + idf.py (real hardware)
make build-zephyr-cortex-a9-qemu       # Zephyr Cortex-A9 QEMU
make build-zephyr-cortex-m3-qemu       # Zephyr Cortex-M3 QEMU (standard QEMU)
make build-linux                       # Linux native (no cross-compile)
make run-linux                         # Build + run directly

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

# Zephyr: first build firmware (generates compile_commands.json),
# then generate Meson cross-file from it
make build-zephyr-cortex-a9-qemu       # CMake configure + build
python3 scripts/gen-zephyr-cross.py qemu_cortex_a9
meson setup build/zephyr-qemu_cortex_a9/meson --cross-file build/zephyr-qemu_cortex_a9/cross.ini
meson compile -C build/zephyr-qemu_cortex_a9/meson
```

## Configuration

All rt-claw business config (AI, Feishu, Telegram, tuning) lives in `claw_config.h` (project root).
Credentials are set via Meson or environment variables — NOT in vendor SDK configs.

```bash
# Environment variables (read at Meson configure time)
export RTCLAW_AI_API_KEY='sk-...'         # AI API key
export RTCLAW_AI_API_URL='https://...'    # API endpoint
export RTCLAW_AI_MODEL='claude-sonnet-4-6'  # Model name
export RTCLAW_FEISHU_APP_ID='cli_...'     # Feishu App ID
export RTCLAW_FEISHU_APP_SECRET='...'     # Feishu App Secret
export RTCLAW_TELEGRAM_BOT_TOKEN='...'    # Telegram Bot token

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

# Zynq-A9 QEMU (FreeRTOS + FreeRTOS+TCP)
make run-zynq-a9-qemu                 # build + launch QEMU (Cadence GEM network)
make run-zynq-a9-qemu GDB=1          # debug mode (GDB port 1234)

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

# Zephyr Cortex-A9 (requires Xilinx QEMU fork)
make run-zephyr-cortex-a9-qemu        # build + launch QEMU
make run-zephyr-cortex-a9-qemu GDB=1  # debug mode (GDB port 1234)

# Zephyr Cortex-M3 (standard QEMU)
make run-zephyr-cortex-m3-qemu        # build + launch QEMU

# Linux native (no QEMU, no cross-compile)
make build-linux                       # meson setup + compile
make run-linux                         # build + run directly
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
- **`-Werror` is enabled project-wide** — all compiler warnings are errors.
  Only our code (claw/, osal/, drivers/, platform/) is affected; vendor/third-party
  libraries are excluded. Never introduce code that produces warnings.

Full reference: [docs/en/coding-style.md](docs/en/coding-style.md)

## Commit Convention

Format: `subsystem: short description` (max 76 chars), body wrapped at 76 chars.

Every commit **must** include `Signed-off-by` (`git commit -s`).

Subsystem prefixes: `osal`, `gateway`, `swarm`, `net`, `ai`, `platform`, `build`, `docs`, `tools`, `scripts`, `main`, `drivers`, `sched`, `feishu`, `telegram`, `ci`, `claw`, `tests`.

## Parallel Branch Development

Use `git worktree` to work on multiple branches simultaneously without conflicts:

```bash
# Create a worktree for another branch (outside project root)
git worktree add ../rt-claw-<topic> <branch-name>

# Example: fix WiFi on a separate branch while zynq work continues
git worktree add ../rt-claw-wifi-fix fix/wifi-reconnect

# Clean up when done
git worktree remove ../rt-claw-wifi-fix
```

## Checks

```bash
# Style check (claw/, osal/, include/ only)
scripts/check-patch.sh                 # all source files
scripts/check-patch.sh --staged        # staged changes only
scripts/check-patch.sh --file <path>   # specific file
make check-agent-skills                # validate .agents skill metadata

# DCO (Signed-off-by) check
scripts/check-dco.sh                   # commits since main
scripts/check-dco.sh --last 3          # last 3 commits

# Install git hooks (pre-commit + commit-msg)
scripts/install-hooks.sh
```

## Testing

```bash
# Unit tests (Linux native, no QEMU needed)
make test-unit-linux                   # build + run all suites

# Functional tests (Python unittest, QEMU boot/shell)
make test-smoke-esp32c3                # ESP32-C3 smoke tests
make test-smoke-esp32s3                # ESP32-S3 smoke tests
make test-smoke-vexpress               # vexpress-a9 boot test
make test-smoke-zynq                   # zynq-a9 boot test
make test-smoke-zephyr-cortex-a9       # Zephyr Cortex-A9 smoke test
make test-smoke-zephyr                 # Zephyr Cortex-M3 smoke test
```

Verify changes by:

1. **All platforms must build successfully before committing.** Run:
   ```bash
   make build-linux
   make vexpress-a9-qemu
   make build-zynq-a9-qemu
   # ESP32 requires: source ~/esp/esp-idf/export.sh
   bash -c 'source ~/esp/esp-idf/export.sh >/dev/null 2>&1 && make build-esp32c3-qemu'
   make build-zephyr-cortex-a9-qemu
   make build-zephyr-cortex-m3-qemu
   ```
2. `scripts/check-patch.sh --staged` passes
3. `make check-agent-skills` passes when `.agents/` changes
4. QEMU boot test: `make run-vexpress-a9-qemu` or `make run-esp32c3-qemu` or `make run-zynq-a9-qemu`

## Agent Skills

Agent-agnostic development skills live under `.agents/skills/`. Use them when
working with tools that support repository-local skills, or read the matching
`SKILL.md` directly for task-specific guidance.

## Key Paths

| Path | Purpose |
|------|---------|
| `Makefile` | Unified build entry point |
| `meson.build` | Root Meson project (cross-compiles claw/ + osal/) |
| `meson_options.txt` | Build options (osal backend, feature flags, AI/Feishu/Telegram config) |
| `build/<platform>/` | Build outputs (gitignored) |
| `include/` | Unified public headers (claw_os.h, claw_net.h, etc.) |
| `include/platform/` | Platform abstraction interfaces (board.h, ota.h, net.h) |
| `include/drivers/` | Driver public headers (mirror of drivers/ structure) |
| `include/utils/` | Utility headers (bitops.h, list.h) |
| `drivers/net/espressif/` | Espressif WiFi driver (shared across C3/S3) |
| `drivers/audio/espressif/` | ES8311 audio codec driver |
| `drivers/display/espressif/` | SSD1306 OLED display driver |
| `drivers/serial/espressif/` | Serial console driver |
| `osal/freertos/` | FreeRTOS OSAL implementation |
| `osal/rtthread/` | RT-Thread OSAL implementation |
| `osal/zephyr/` | Zephyr OSAL implementation |
| `osal/linux/` | Linux OSAL (pthreads + libcurl + file KV) |
| `vendor/lib/cjson/` | cJSON library |
| `vendor/os/freertos/` | FreeRTOS-Kernel (submodule) |
| `vendor/os/rt-thread/` | RT-Thread (submodule) |
| `vendor/os/zephyr/` | Zephyr RTOS (submodule) |
| `include/claw/core/class.h` | OOP infrastructure (container_of, registration macros, ops validation) |
| `include/claw/core/errno.h` | Unified error codes (`claw_err_t` enum + `claw_strerror()`) |
| `include/claw/core/service.h` | Service base class (ops vtable, lifecycle state machine, deps) |
| `include/claw/core/driver.h` | Driver base class (probe/remove lifecycle) |
| `include/claw/core/tool.h` | Tool base class (ops vtable) |
| `include/claw/core/console.h` | Console output with capture (claw_printf) |
| `claw/core/service.c` | Service registry, topological sort, lifecycle management |
| `claw/core/driver.c` | Driver registry, probe/remove lifecycle |
| `claw/core/tool.c` | Tool registry, linker section collection |
| `claw/init.c` | Boot entry: collect drivers/services/tools, probe, start |
| `claw_config.h` | Unified compile-time configuration (project root) |
| `claw_gen_config.h.in` | Meson template for generated config header |
| `claw/services/gateway.c` | Message router with OOP context, polymorphic dispatch |
| `claw/services/sched.c` | Task scheduler service |
| `claw/services/heartbeat.c` | Periodic AI heartbeat service |
| `claw/services/{ai,net,swarm,im,ota}/` | Service modules (OOP context structs, deps declaration) |
| `claw/services/tools/` | Tool Use framework (tools declare SWARM_CAP_* and CLAW_TOOL_LOCAL_ONLY) |
| `platform/common/espressif/` | Shared Espressif board helpers (WiFi init + shell) |
| `platform/esp32c3/` | ESP32-C3 unified ESP-IDF project (all boards) |
| `platform/esp32c3/boards/` | Board-specific configs (qemu, devkit, xiaozhi-xmini) |
| `platform/esp32s3/` | ESP32-S3 unified ESP-IDF project (all boards) |
| `platform/esp32s3/boards/` | Board-specific configs (qemu, default) |
| `platform/linux/` | Linux native platform (pthreads, libcurl, stdin shell) |
| `platform/zynq-a9/` | Zynq-A9 QEMU (FreeRTOS + FreeRTOS+TCP, Cadence GEM) |
| `platform/zynq-a9/drivers/` | Patched Zynq GEM NetworkInterface (ISR/PHY fixes) |
| `platform/zephyr/` | Zephyr platform (Zephyr Module wrapper) |
| `platform/zephyr/boards/` | Board-specific configs (qemu_cortex_a9, qemu_cortex_m3) |
| `platform/vexpress-a9/` | RT-Thread BSP + Meson cross-file |
| `vendor/bsp/xilinx/` | Xilinx standalone BSP subset (GIC, Timer, EMAC PS) |
| `vendor/lib/freertos-plus-tcp/` | FreeRTOS+TCP networking stack (submodule) |
| `scripts/gen-esp32c3-cross.py` | Generate ESP32-C3 Meson cross-file |
| `scripts/gen-esp32s3-cross.py` | Generate ESP32-S3 Meson cross-file |
| `scripts/gen-zephyr-cross.py` | Generate Zephyr Meson cross-file |
| `scripts/api-proxy.py` | HTTP→HTTPS proxy for QEMU without TLS |
