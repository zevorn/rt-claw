---
name: platform-port
description: Guide porting RT-Claw to a new hardware platform or RTOS. Use when user wants to "add a new platform", "port to a new board", "support new hardware", or "add a new RTOS backend".
disable-model-invocation: true
user-invocable: true
argument-hint: "<platform-name> [rtos: freertos|rtthread]"
---

# Port RT-Claw to a New Platform

Step-by-step guide for adding a new hardware platform or board variant.

## Arguments

$ARGUMENTS

Format: `<platform-name> [rtos]`
- `platform-name`: e.g., `esp32c6`, `stm32h7`, `rpi-pico`
- `rtos`: `freertos` (default) or `rtthread`

## Context

- Existing platforms: !`ls -d platform/*/`
- OSAL backends: !`ls osal/`
- Board abstraction: !`head -40 include/claw_board.h 2>/dev/null`
- Cross-file examples: !`ls platform/*/cross.ini 2>/dev/null; ls scripts/gen-*-cross.py 2>/dev/null`

## Platform Architecture Reference

```
platform/<name>/                # Platform root
├── boards/                     # Board variants (if multiple)
│   ├── qemu/                   # QEMU virtual board
│   │   ├── sdkconfig.defaults
│   │   └── partitions.csv
│   └── <board>/                # Real hardware board
│       ├── sdkconfig.defaults
│       └── partitions.csv
├── main/                       # Platform entry point
│   └── main.c                  # Calls claw_init()
├── CMakeLists.txt              # Native build system (ESP-IDF: idf.py)
├── SConscript                  # RT-Thread: SCons build
└── cross.ini                   # Meson cross-compilation file
```

## Porting Checklist

### Phase 1: Build Infrastructure

- [ ] Create `platform/<name>/` directory structure
- [ ] Create Meson cross-file (`cross.ini`) or generator script (`scripts/gen-<name>-cross.py`)
- [ ] Add Makefile targets: `build-<name>`, `run-<name>-qemu` (if QEMU available)
- [ ] Verify `meson compile` produces `libclaw.a` and `libosal.a`

### Phase 2: OSAL Backend

If using an existing RTOS with existing OSAL backend (FreeRTOS or RT-Thread):
- [ ] Verify OSAL backend compiles for the new architecture
- [ ] Check timer tick resolution matches expectations
- [ ] Test mutex, semaphore, task creation

If adding a new RTOS:
- [ ] Create `osal/<rtos>/claw_os_<rtos>.c` implementing all APIs in `include/osal/claw_os.h`
- [ ] Create `osal/<rtos>/claw_net_<rtos>.c` implementing `include/osal/claw_net.h`
- [ ] Add RTOS vendor code to `vendor/os/<rtos>/`
- [ ] Add `osal_backend` option value to `meson_options.txt`

### Phase 3: Board Abstraction

- [ ] Implement `board_early_init()` in platform main
- [ ] Implement `board_platform_commands()` for platform-specific shell commands
- [ ] Call `claw_init()` from platform main after board init
- [ ] Configure network: WiFi driver or Ethernet (QEMU: OpenCores/LAN9118)

### Phase 4: Driver Integration

- [ ] Identify available hardware peripherals
- [ ] Create drivers under `drivers/<subsystem>/<vendor>/` if new hardware
- [ ] Reuse existing drivers where possible (e.g., `drivers/net/espressif/` for ESP32 variants)
- [ ] Add driver headers to `include/drivers/<subsystem>/<vendor>/`

### Phase 5: QEMU Support (if available)

- [ ] Configure QEMU machine type and arguments
- [ ] Add QEMU run script or Makefile target
- [ ] Verify network connectivity in QEMU (OpenCores Ethernet or user-mode networking)
- [ ] Test AI API connectivity via `scripts/api-proxy.py`

### Phase 6: Verification

- [ ] Build passes: `make build-<name>`
- [ ] Style check passes: `scripts/check-patch.sh`
- [ ] QEMU boots to shell prompt (if QEMU available)
- [ ] AI chat works via terminal
- [ ] All existing platforms still build (no regressions)

## Workflow

### Step 1: Analyze the target

Identify:
- CPU architecture (RISC-V, ARM Cortex-M/A, Xtensa)
- RTOS choice and SDK toolchain
- Available QEMU machine emulation
- Network capability (WiFi, Ethernet, cellular)
- Memory constraints (Flash, RAM, PSRAM)

### Step 2: Create scaffold

Generate the platform directory structure following the reference above.
Use the closest existing platform as a template:
- ESP32-S3 for new Espressif chips
- ESP32-C3 for new RISC-V platforms
- vexpress-a9 for new RT-Thread platforms

### Step 3: Implement incrementally

Follow the phases above in order. Verify each phase builds before moving to the next.

### Step 4: Update documentation

- Add platform to `CLAUDE.md` Key Paths table
- Add build/run commands to `docs/*/usage.md`
- Update README Quick Start if this is a primary platform
- Add porting notes to `docs/*/porting.md`
