---
name: build-test
description: Build and test rt-claw on QEMU or real hardware. Use when user says "build", "compile", "run QEMU", "test", "burn/flash firmware", or specifies a platform target.
disable-model-invocation: true
user-invocable: true
argument-hint: "[platform] [--gdb] [--graphics]"
---

# Build and Test RT-Claw

Build the project for a specified platform and run it on QEMU or flash to real hardware.

## Arguments

$ARGUMENTS

Supported platforms:
- `esp32c3-qemu` (default)
- `esp32c3-devkit`
- `esp32c3-xiaozhi-xmini`
- `esp32s3-qemu`
- `esp32s3`
- `vexpress-a9-qemu`

Options:
- `--gdb`: Enable GDB debug mode (port 1234)
- `--graphics`: Enable LCD display window (ESP32 QEMU only)

If no platform specified, auto-detect from current branch name or recent changes.

## Context

- Current branch: !`git branch --show-current`
- Changed files: !`git diff --name-only HEAD`
- Staged files: !`git diff --name-only --cached`

## Workflow

### Step 1: Determine target platform

If `$ARGUMENTS` specifies a platform, use it directly.

Otherwise, infer from context:
1. Check branch name for platform hints (e.g., `esp32c3`, `esp32s3`, `vexpress`)
2. Check changed files for platform-specific paths (e.g., `platform/esp32c3/`, `osal/rtthread/`)
3. Default to `esp32c3-qemu` if ambiguous

### Step 2: Run style check

```bash
scripts/check-patch.sh --staged
```

If there are style violations, report them but do NOT block the build.

### Step 3: Build

Run the appropriate build command:

| Platform | Build Command |
|----------|--------------|
| esp32c3-qemu | `make build-esp32c3-qemu` |
| esp32c3-devkit | `make build-esp32c3-devkit` |
| esp32c3-xiaozhi-xmini | `make build-esp32c3-xiaozhi-xmini` |
| esp32s3-qemu | `make build-esp32s3-qemu` |
| esp32s3 | `make build-esp32s3` |
| vexpress-a9-qemu | `make vexpress-a9-qemu` |

If the build fails:
1. Show the first error message
2. Analyze the root cause (missing header, link error, type mismatch, etc.)
3. Suggest a fix
4. Do NOT auto-fix unless the user confirms

### Step 4: Run (QEMU platforms only)

For QEMU platforms, launch the emulator:

| Platform | Run Command |
|----------|------------|
| esp32c3-qemu | `make run-esp32c3-qemu` |
| esp32s3-qemu | `make run-esp32s3-qemu` |
| vexpress-a9-qemu | `make run-vexpress-a9-qemu` |

Add `GDB=1` if `--gdb` was specified.
Add `GRAPHICS=1` if `--graphics` was specified.

For real hardware platforms:
- `esp32c3-devkit` / `esp32c3-xiaozhi-xmini`: `make flash-esp32c3-<board>`
- `esp32s3`: `make flash-esp32s3`

Inform the user to connect the board and monitor serial output.

### Step 5: Report results

Summarize:
- Build status (success/failure)
- Binary size (if available)
- QEMU boot status (if applicable)
- Any warnings worth noting
