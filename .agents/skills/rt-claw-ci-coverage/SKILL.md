---
name: rt-claw-ci-coverage
description: Use when adding CI coverage for a new rt-claw module, feature chain, endpoint backend, service integration, or regression.
license: MIT
---

# rt-claw CI Coverage

Add the narrowest deterministic check that proves the new behavior is wired into
CI, then broaden only when shared code or platform behavior changed.

## When Coverage Is Required

Add or update CI coverage when a change introduces any of these:

- a new service, driver, tool, OSAL backend, endpoint backend, or platform port
- a new feature chain that crosses module boundaries
- a new public API, shell command, Meson/Kconfig option, or Makefile target
- a bug fix where the failure can be reproduced without secrets or hardware
- behavior that must not regress across Linux, QEMU, or supported boards

Do not require real credentials, cloud services, physical hardware, or local
operator input for mandatory PR CI. Cover those paths with mocks, stubs,
compile-only checks, Linux-native tests, QEMU smoke tests, or optional online CI.

## Coverage Ladder

Choose the first rung that can express the behavior without flakiness:

1. Unit test: pure logic, parsers, state transitions, registries, public helpers.
2. Linux functional test: shell commands, KV persistence, process lifecycle.
3. Linux option build: Meson options, optional sources, platform helpers.
4. QEMU smoke test: boot, shell availability, platform integration.
5. Hardware or online test: only optional/manual when hardware, network, or
   credentials are required.

Prefer one strong narrow test over a broad slow test that only proves the code
compiled.

## Placement

| Change | Preferred coverage |
|--------|--------------------|
| Pure helper or parser | `tests/unit/test_<area>.c` |
| Service state machine or registry | `tests/unit/` with stubs where needed |
| Shell command or Linux process behavior | `tests/functional/test_linux_*.py` |
| Meson option or optional backend | `scripts/test-meson-matrix.sh` or a targeted workflow step |
| QEMU-visible platform behavior | existing `test-smoke-*` target |
| Agent skill guidance | `make check-agent-skills` |
| Documentation-only behavior | no CI unless examples are executable |

## Workflow

1. Identify the smallest behavior that must never regress.
2. Identify what CI can run without secrets, hardware, or operator input.
3. Add the test in the owning area, not in an unrelated subsystem.
4. Wire the test into an existing target before adding a workflow.
5. Add a workflow job only when no existing target or matrix covers the new path.
6. Keep path filters updated if the workflow must run for new files.
7. Run the exact CI command locally when practical.
8. Report any coverage intentionally left manual or optional.

## Existing CI Entry Points

- `make test-unit-linux`: Linux unit tests; preferred for services and helpers.
- `make test-linux`: Linux functional tests after `make build-linux`.
- `make test-smoke-linux`: build plus Linux functional tests.
- `scripts/test-meson-matrix.sh`: Meson option compile matrix.
- `make test-smoke-esp32c3`, `make test-smoke-vexpress`, `make test-smoke-zynq`:
  QEMU smoke checks.
- `make check-agent-skills`: validate `.agents/skills/*/SKILL.md` metadata.

## Feature Chain Guidance

When a feature crosses services, endpoints, tools, platforms, or external
providers, add a chain-specific subsection using this shape:

```text
## <Feature> Chain Guidance

For <feature> work, split mandatory CI from optional integration:

- Mandatory CI should cover <stable local contracts>.
- Do not call real <external providers or hardware> in required PR CI.
- Do not require <feature-specific secrets, devices, permissions, or boards> in
  required PR CI.
- Use <unit / Linux functional / option matrix / QEMU target> to cover
  <source wiring and deterministic behavior>.
- Use optional/manual tests for <real provider, hardware, browser, or network
  verification>.
```

Use concrete nouns. Avoid vague entries like "test the chain"; name the public
API, event, parser, shell command, option, state transition, or backend callback
that CI can assert.

### Voice Chain Example

For voice work, split mandatory CI from optional integration:

- Mandatory CI should cover endpoint attach/detach, endpoint callbacks, event
  validation, shell command wiring, option builds, and buffer/state behavior.
- Do not call real STT/TTS providers in required PR CI.
- Do not require USB microphones, speakers, browser microphone permissions, or
  Raspberry Pi hardware in required PR CI.
- Use Linux option builds to cover `voice`, `linux_web_voice`, and
  `linux_local_voice` source wiring.
- Use optional/manual tests for real microphone, speaker, browser, and cloud
  provider verification.

## Rules

- Do not add a new test framework without explicit request.
- Do not silently skip a test that should be mandatory.
- Do not make mandatory CI depend on secrets, network availability, wall-clock
  timing, real hardware, or public cloud APIs.
- Do not add production test hooks unless an existing project pattern already
  supports them.
- Do not weaken assertions just to stabilize a flaky test; narrow the scope or
  move the check to optional CI instead.
