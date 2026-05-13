---
name: rt-claw-testing
description: Use when choosing, adding, or running rt-claw unit tests, functional tests, smoke tests, QEMU checks, or PR validation commands.
license: MIT
---

# rt-claw Testing

Start with the narrowest test that exercises the changed behavior, then broaden
before submission when shared code changed.

## Commands

| Scope | Command |
|-------|---------|
| Unit tests on Linux | `make test-unit-linux` |
| Linux functional tests | `make test-linux` |
| Linux smoke tests | `make test-smoke-linux` |
| ESP32-C3 smoke tests | `make test-smoke-esp32c3` |
| ESP32-S3 smoke tests | `make test-smoke-esp32s3` |
| vexpress-a9 smoke tests | `make test-smoke-vexpress` |
| Zynq-A9 smoke tests | `make test-smoke-zynq` |
| Zephyr Cortex-A9 smoke tests | `make test-smoke-zephyr-cortex-a9` |
| Zephyr Cortex-M3 smoke tests | `make test-smoke-zephyr` |
| Style check | `scripts/check-patch.sh --staged` |

## Workflow

1. Identify the behavior and owning subsystem.
2. For bug fixes, reproduce the failure first when practical.
3. Add or update focused regression coverage if the test harness can express
   the behavior.
4. Run the narrowest useful test while iterating.
5. Run a relevant platform build before PR submission.
6. Report skipped tests, missing dependencies, or hardware requirements.

## Test Placement

- Unit tests live under `tests/unit/`.
- Functional tests live under `tests/functional/`.
- QEMU boot and shell behavior should use existing functional harness patterns.
- Do not add a new test framework without an explicit request.

## Rules

- Do not weaken assertions to match broken behavior.
- Do not silently skip tests.
- Do not add test-only logic to production paths unless guarded by existing
  project patterns.
- Do not require real hardware for checks when a QEMU or Linux-native path can
  cover the same behavior.
