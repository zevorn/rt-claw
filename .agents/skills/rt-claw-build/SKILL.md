---
name: rt-claw-build
description: Use when building rt-claw, choosing a platform target, running QEMU, flashing hardware, or debugging C/Meson/native build failures.
license: MIT
---

# rt-claw Build

Use the repository `Makefile` before inventing ad hoc commands. Meson builds
the platform-independent libraries; each platform target then uses its native
build system when needed.

## Quick Commands

| Task | Command |
|------|---------|
| Linux native build | `make build-linux` |
| Linux native run | `make run-linux` |
| Unit tests on Linux | `make test-unit-linux` |
| ESP32-C3 QEMU build | `make build-esp32c3-qemu` |
| ESP32-C3 QEMU run | `make run-esp32c3-qemu` |
| ESP32-C3 devkit build | `make build-esp32c3-devkit` |
| ESP32-C3 xiaozhi-xmini build | `make build-esp32c3-xiaozhi-xmini` |
| ESP32-C3 xiaozhi-xmini flash | `make flash-esp32c3-xiaozhi-xmini` |
| ESP32-S3 QEMU build | `make build-esp32s3-qemu` |
| ESP32-S3 hardware build | `make build-esp32s3` |
| ESP32-S3 hardware flash | `make flash-esp32s3` |
| vexpress-a9 QEMU build | `make vexpress-a9-qemu` |
| Zynq-A9 QEMU build | `make build-zynq-a9-qemu` |
| Zephyr Cortex-M3 QEMU build | `make build-zephyr-cortex-m3-qemu` |
| Zephyr Cortex-A9 QEMU build | `make build-zephyr-cortex-a9-qemu` |
| Style check | `scripts/check-patch.sh --staged` |

## Run Options

- Pass `GDB=1` to QEMU run targets to start in debug mode on port 1234.
- Pass `GRAPHICS=1` to ESP32 QEMU run targets when an LCD window is needed.
- Real hardware targets require the board to be connected before flash or
  monitor commands run.

## Target Selection

1. Use the platform named by the user.
2. If no platform is named, inspect changed paths:
   - `platform/linux/`, `osal/linux/`, or unit tests -> `make build-linux`
   - `platform/esp32c3/` or Espressif C3 drivers -> `make build-esp32c3-qemu`
   - `platform/esp32s3/` or shared Espressif changes -> `make build-esp32s3-qemu`
   - `platform/vexpress-a9/` or `osal/rtthread/` -> `make vexpress-a9-qemu`
   - `platform/zynq-a9/` or `osal/freertos/` -> `make build-zynq-a9-qemu`
   - `platform/zephyr/` or `osal/zephyr/` -> a Zephyr QEMU build target
3. Check branch names for platform hints such as `esp32c3`, `esp32s3`,
   `vexpress`, `zynq`, or `zephyr`.
4. If ambiguous, use `make build-linux` for the fastest compile check.

## Workflow

1. Confirm branch and worktree with `git status -sb`.
2. Read `CLAUDE.md` for current platform commands before changing a command.
3. Run the narrowest useful build while iterating.
4. Run `scripts/check-patch.sh --staged` before committing staged C changes.
5. For PR submission, build at least one affected platform. Broaden to more
   platforms when changing shared `claw/`, `include/`, `osal/`, `drivers/`, or
   build-system files.

## Failure Handling

- Preserve the first compiler or linker error; later failures may be follow-on
  noise.
- Report style failures from `scripts/check-patch.sh --staged`, but do not
  hide the build result behind style output.
- Check Meson source lists when a symbol is undefined or a new file is not
  compiled.
- Check include order and OSAL boundaries before adding headers.
- Do not edit unrelated platforms just because a matrix build visits them.
- Keep `build/` artifacts out of commits.
