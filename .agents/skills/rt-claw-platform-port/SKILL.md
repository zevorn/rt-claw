---
name: rt-claw-platform-port
description: Use when porting rt-claw to a new hardware platform, board variant, SoC, or RTOS backend.
license: MIT
---

# rt-claw Platform Port

Port incrementally. Get the library build working first, then platform boot,
then drivers, then QEMU or hardware validation.

## Starting Questions

Identify these before editing:

- CPU architecture, SoC, and board name.
- RTOS choice: FreeRTOS, RT-Thread, Zephyr, Linux, or a new backend.
- SDK and compiler toolchain.
- Available QEMU machine, if any.
- Network path: WiFi, Ethernet, cellular, or proxy-only.
- Memory constraints: flash, RAM, and PSRAM.

## Platform Layout

```text
platform/<name>/
+-- boards/
|   +-- qemu/
|   |   +-- sdkconfig.defaults
|   |   +-- partitions.csv
|   +-- <board>/
|       +-- sdkconfig.defaults
|       +-- partitions.csv
+-- main/
|   +-- main.c
+-- CMakeLists.txt
+-- SConscript
+-- cross.ini
```

Use only the files that fit the target platform. ESP-IDF ports use CMake;
RT-Thread ports use SCons; some platforms use generated Meson cross files.

## Porting Phases

1. Build infrastructure:
   - create the platform directory structure
   - add a Meson cross file or generator script when needed
   - add Makefile build and run targets
   - verify Meson produces the expected rt-claw libraries
2. OSAL:
   - reuse an existing backend when possible
   - check task, mutex, semaphore, timer, KV, and network behavior
   - for a new RTOS, implement the OSAL under `osal/<rtos>/`
3. Board abstraction:
   - implement early board initialization in platform code
   - call `claw_init()` after board setup
   - add platform shell commands only for platform-specific behavior
4. Drivers:
   - reuse existing drivers where possible
   - put new hardware drivers under `drivers/<subsystem>/<vendor>/`
   - put public driver headers under `include/drivers/...`
5. QEMU or hardware:
   - configure QEMU machine and networking when available
   - add flash and monitor targets for real hardware
   - use `scripts/api-proxy.py` for QEMU HTTP/TLS limitations when needed
6. Documentation:
   - update build/run commands
   - update platform notes in paired EN/ZH docs when user-facing
   - update agent guidance if new workflow commands are introduced

## Closest Templates

- New Espressif RISC-V boards: start from `platform/esp32c3/`.
- New Espressif S3-style boards: start from `platform/esp32s3/`.
- RT-Thread QEMU work: start from `platform/vexpress-a9/`.
- FreeRTOS ARM QEMU work: start from `platform/zynq-a9/`.
- Zephyr boards: start from `platform/zephyr/`.

## Verification

- `make build-<name>` or the closest new target.
- `scripts/check-patch.sh --staged`.
- QEMU boots to a shell prompt when QEMU is available.
- AI/API paths work only when credentials and network are available; do not
  commit credentials to make them work.
- Existing shared platforms still build when shared code changed.

## Rules

- Do not modify `vendor/` submodules.
- Do not put application logic in `platform/` or `osal/`.
- Do not add a new OSAL backend when an existing backend can serve the target.
- Do not introduce broad platform abstractions before one working platform
  needs them.
