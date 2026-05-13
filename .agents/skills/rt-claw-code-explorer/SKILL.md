---
name: rt-claw-code-explorer
description: Use when finding rt-claw definitions, call sites, subsystem ownership, build wiring, OSAL boundaries, or platform-specific implementations.
license: MIT
---

# rt-claw Code Explorer

Search from the repository root and identify the owning layer before editing.
rt-claw is split between platform-independent logic, OSAL backends, platform
BSPs, and vendor code.

## Search Order

1. Use `rg` or `rg --files` from the repository root.
2. Use `meson.build`, `meson_options.txt`, and platform build files to confirm
   compilation ownership.
3. Use `git log -- <path>` when subsystem history matters.
4. Read `AGENTS.md` and `CLAUDE.md` before changing architecture, build, or
   commit behavior.
5. Check both `docs/en/` and `docs/zh/` when changing documented behavior.

## Repository Map

| Path | Purpose |
|------|---------|
| `claw/` | Platform-independent core, services, shell, and Tool Use logic |
| `include/` | Public headers and OSAL-facing APIs |
| `osal/freertos/` | FreeRTOS OSAL implementation |
| `osal/rtthread/` | RT-Thread OSAL implementation |
| `osal/zephyr/` | Zephyr OSAL implementation |
| `osal/linux/` | Linux OSAL for native tests and tools |
| `drivers/` | Hardware drivers |
| `platform/` | Board support and native build integration |
| `scripts/` | Build, check, QEMU, OTA, and cross-file helpers |
| `tests/unit/` | Linux-native C unit tests |
| `tests/functional/` | QEMU and Linux functional tests |
| `vendor/` | Third-party submodules; do not modify |

## Boundary Rules

- Code under `claw/` must not include FreeRTOS, RT-Thread, Zephyr, ESP-IDF, or
  other RTOS/vendor headers directly. Use `claw_os.h` and public project
  abstractions.
- RTOS-specific code belongs under `osal/<rtos>/`.
- Board-specific glue belongs under `platform/<board>/`.
- Shared board helpers belong under `platform/common/<vendor>/`.
- Hardware drivers belong under `drivers/<subsystem>/<vendor>/` with public
  headers under `include/drivers/<subsystem>/<vendor>/`.
- Do not put application logic in `platform/` or `osal/`.

## Rules

- Prefer existing local helpers and service/driver/tool patterns.
- Keep changes minimal and scoped to the owning subsystem.
- Do not modify `vendor/` submodules.
- Do not guess API behavior. Read the source first.
