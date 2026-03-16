---
name: new-module
description: Scaffold a new claw service, driver, or tool module. Use when user wants to "add a new service", "create a driver", "implement a new module", or "add tool use support".
disable-model-invocation: true
user-invocable: true
argument-hint: "<type> <name> [description]"
---

# Create New RT-Claw Module

Scaffold a new module following the project's established architecture patterns.

## Arguments

$ARGUMENTS

Format: `<type> <name> [description]`

- `type`: One of `service`, `driver`, `tool`
- `name`: Module name in snake_case (e.g., `bluetooth`, `audio_tts`, `weather`)
- `description`: Optional one-line description

## Context

- Existing services: !`ls claw/services/`
- Existing tools: !`ls claw/tools/*.c 2>/dev/null`
- Existing drivers: !`ls -d drivers/*/ 2>/dev/null`
- Meson options: !`head -60 meson_options.txt`
- Init registration: !`grep -n 'service_start\|register' claw/claw_init.c | head -20`

## Module Architecture Reference

### Service Module Pattern (claw/services/<subsystem>/)

Based on existing services (ai, net, swarm, im):

1. **Header** (`claw/services/<name>/<name>.h`):
   - Header guard: `CLAW_SERVICES_<NAME>_H`
   - Include `claw_os.h` first
   - Public API: `<name>_init()`, `<name>_start()`, `<name>_stop()`
   - SPDX license header

2. **Source** (`claw/services/<name>/<name>.c`):
   - Include order: own header -> `claw_os.h` -> system headers -> project headers
   - Static internal state (no global mutable state)
   - Thread-safe design (mutex for shared state)
   - Task creation via `claw_task_create()`

3. **Meson integration** (`claw/services/<name>/meson.build`):
   - Conditional compilation via feature option
   - Add source files to `claw_sources`

4. **Registration** (`claw/claw_init.c`):
   - `#include` the header (guarded by feature macro)
   - Call `<name>_init()` in `claw_init()`

5. **Configuration** (`meson_options.txt`):
   - Add `option('enable_<name>', type: 'boolean', value: false, description: '...')`

### Driver Module Pattern (drivers/<subsystem>/<vendor>/)

Based on existing drivers (net/espressif, audio/espressif, display/espressif):

1. **Header** (`include/drivers/<subsystem>/<vendor>/<name>.h`)
2. **Source** (`drivers/<subsystem>/<vendor>/<name>.c`)
3. Thin wrapper around vendor SDK, exposing clean API
4. No business logic — hardware abstraction only

### Tool Module Pattern (claw/tools/)

Based on existing Tool Use framework:

1. **Source** (`claw/tools/tool_<name>.c`)
2. Register via `CLAW_TOOL_REGISTER()` macro
3. Declare capabilities with `SWARM_CAP_*` flags
4. Implement `tool_<name>_execute()` callback

## Workflow

### Step 1: Validate arguments

Parse `$ARGUMENTS` into type, name, and optional description.
Verify no existing module conflicts with the name.

### Step 2: Create files

Generate the module files following the patterns above.
Apply project code style:
- C99, 4-space indent, ~80 char lines
- `snake_case` functions, `CamelCase` structs, `UPPER_CASE` constants
- `/* C style comments only */`
- SPDX MIT license header

### Step 3: Wire up build system

- Add `meson.build` in the module directory
- Add feature option to `meson_options.txt`
- Include the meson.build from parent directory

### Step 4: Register in init

Add the module initialization call to `claw/claw_init.c`, guarded by the feature macro.

### Step 5: Verify build

Run `meson compile -C build/esp32c3-qemu/meson` to verify the new module compiles.

### Step 6: Summary

Show the created files and suggest next steps:
- Implement the actual business logic
- Add shell commands if interactive
- Add QEMU test coverage
- Update documentation
