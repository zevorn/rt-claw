# rt-claw Coding Style

**English** | [中文](../zh/coding-style.md)

This document defines the C coding style for the rt-claw project.
It applies to all code under `claw/`, `osal/`, and `include/` — the
platform-independent core and the OS abstraction layer.

Use `scripts/check-patch.sh` to verify compliance before submitting.

## Scope

| Directory   | Checked | Notes                              |
|-------------|---------|-------------------------------------|
| `claw/`     | Yes     | Core services, gateway, AI engine   |
| `include/`  | Yes     | Public headers                      |
| `osal/`     | Yes     | OS abstraction layer                |
| `platform/` | No      | Platform-specific BSP/build files   |
| `vendor/`   | No      | Third-party RTOS source code        |
| `scripts/`  | No      | Build and launch scripts            |

## Whitespace and Indentation

- **4 spaces** for indentation. **Never tabs** in C source.
- Do not leave trailing whitespace.
- The repository includes an `.editorconfig` file; most editors support it
  natively or via plugin.

## Line Width

- Target: **80 characters**.
- If wrapping at 80 makes code less readable, go up to ~100 but no further.
- `checkpatch.pl` warns at 100 characters.

## Naming

### Variables

`lower_case_with_underscores` — easy to type and read.

### Functions

- Public API uses the `claw_` prefix: `claw_os_thread_create()`, `claw_gateway_send()`.
- OSAL functions use `claw_os_` prefix: `claw_os_mutex_lock()`.
- Service-internal functions use their subsystem prefix: `gateway_route_msg()`, `swarm_elect_leader()`.
- Static (file-local) functions need no prefix, but a descriptive name is required.
- Use `_locked` suffix for functions that expect a lock to be held by the caller.

### Types

- Struct/enum type names use `CamelCase`: `GatewayMsg`, `SwarmNodeState`.
- Scalar typedefs use `lower_case_t`: `claw_err_t`, `node_id_t`.
- Enum values use `UPPER_CASE`: `CLAW_OK`, `CLAW_ERR_TIMEOUT`.

## Block Structure

Every indented statement is braced, even single-statement blocks:

```c
if (a == 5) {
    do_something();
} else {
    do_other();
}
```

Opening brace is on the same line as the control statement.
Exception: function definitions put the opening brace on its own line:

```c
void claw_gateway_init(void)
{
    /* ... */
}
```

## Declarations

- Declarations at the top of blocks; do not mix with statements.
- Loop variables may be declared in `for`:

```c
for (int i = 0; i < count; i++) {
    /* ... */
}
```

## Conditional Statements

Constant on the right:

```c
if (ret == CLAW_OK) {    /* good — reads naturally */
    ...
}
```

## Comment Style

Use traditional C comments `/* */`. Avoid `//`.

Multi-line comments:

```c
/*
 * Multi-line comment
 * uses stars on the left.
 */
```

## Include Order

```c
#include "osal/claw_os.h"  /* OSAL header first (for source in claw/) */
#include <stdint.h>       /* then system/standard headers */
#include <string.h>
#include "claw/core/gateway.h" /* then project headers */
```

For OSAL implementation files (`osal/freertos/`, `osal/rtthread/`),
include the RTOS header before the OSAL header:

```c
#include <rtthread.h>     /* RTOS header */
#include "osal/claw_os.h"  /* OSAL interface */
```

## C Standard

Write to **C99** (gnu99). Avoid compiler-specific extensions beyond
what GCC and Clang both support.

## Types

- Use `<stdint.h>` fixed-width types: `uint8_t`, `int32_t`, etc.
- Use `size_t` for memory sizes, `ssize_t` for signed sizes.
- Use `bool` from `<stdbool.h>` for boolean values.
- Avoid bare `int` / `long` when a more specific type is appropriate.
- Ensure pointers are `const`-correct.

## Memory Management

Use RTOS-provided allocation through OSAL, or standard `malloc`/`free`.
Never call RTOS-specific allocators directly from `claw/` code.

## String Safety

- Never use `strncpy` (does not guarantee null termination).
- Use `snprintf` instead of `sprintf`.
- Use `strlcpy` / `strlcat` if available, or write bounded copies explicitly.

## Error Handling

- Functions return `claw_err_t` for fallible operations.
- Check return values. Do not silently ignore errors.
- Use `CLAW_OK` (0) for success; negative values for errors.

## Preprocessor

- Header guards use the pattern `CLAW_<PATH>_<NAME>_H`:

```c
#ifndef CLAW_CORE_GATEWAY_H
#define CLAW_CORE_GATEWAY_H
/* ... */
#endif /* CLAW_CORE_GATEWAY_H */
```

- Variadic macros:

```c
#define CLAW_LOG(fmt, ...) \
    claw_os_printf("[CLAW] " fmt, ##__VA_ARGS__)
```

## Running the Checker

```bash
# Check all files in claw/, osal/, and include/
scripts/check-patch.sh

# Check specific files
scripts/check-patch.sh --file claw/core/gateway.c include/osal/claw_os.h

# Check staged changes only
scripts/check-patch.sh --staged

# Check commits since main
scripts/check-patch.sh --branch main
```
