---
name: rt-claw-new-module
description: Use when adding a new rt-claw service, driver, tool, platform helper, platform port, or RTOS backend.
license: MIT
---

# rt-claw New Module

Create new modules by copying the closest existing pattern and wiring only the
minimum required build and init paths.

## Module Types

| Type | Owning path |
|------|-------------|
| Service | `claw/services/<name>/` |
| Tool Use tool | `claw/services/tools/<name>.c` |
| Hardware driver | `drivers/<subsystem>/<vendor>/` and `include/drivers/...` |
| Platform helper | `platform/common/<vendor>/` |
| Board/platform port | `platform/<board>/` |
| RTOS backend | `osal/<rtos>/` |

## Workflow

1. Confirm the requested module type and name.
2. Search for the closest existing implementation.
3. Check for name or path conflicts.
4. Add only the files required for the first working vertical slice.
5. Add SPDX MIT headers to every new `.c` and `.h` file.
6. Wire the module into Meson, native platform build files, and init paths only
   where needed.
7. Add configuration options using `CLAW_<SUBSYSTEM>_<PARAM>` naming for config
   macros.
8. Build the narrowest affected platform.

Use snake_case names such as `bluetooth`, `audio_tts`, or `weather`. For
services, the normal public lifecycle names are `<name>_init()`,
`<name>_start()`, and `<name>_stop()` when the module exposes them directly.

## Service Rules

- Platform-independent service code belongs under `claw/services/<name>/`.
- Include `claw_os.h` instead of RTOS headers.
- Use header guards shaped like `CLAW_SERVICES_<NAME>_H`.
- Include the module's own header first in its `.c` file, followed by
  `claw_os.h`, system headers, and project headers.
- Use existing service registration, dependency, and lifecycle patterns.
- Keep mutable state private to the module.
- Use mutexes or existing OSAL primitives for shared state.
- Add the module source to the owning `meson.build` and guard optional modules
  with existing feature-option patterns.

## Driver Rules

- Driver code exposes hardware access only; do not put application behavior in
  drivers.
- Public headers mirror the driver path under `include/drivers/`.
- Vendor SDK usage belongs in driver or platform-specific layers, not `claw/`.
- Keep drivers thin around vendor SDKs and expose a clean project API.

## Tool Rules

- Tool Use modules belong under `claw/services/tools/`.
- Use existing `CLAW_TOOL_REGISTER()` and capability patterns.
- Implement the tool execute callback following neighboring tool modules.
- Mark slow, local-only, or platform-specific tools consistently with existing
  flags.

## Rules

- Do not modify `vendor/`.
- Do not add a new framework for one module.
- Do not add compatibility shims for unused old APIs.
- Do not add tests in unrelated subsystems.
