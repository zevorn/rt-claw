# rt-claw Agent Guide

This file is the shared source of project instructions for AI agents and
human contributors working in this repository. Tool-specific files such as
`CLAUDE.md`, `GEMINI.md`, and `.github/copilot-instructions.md` should point
back here and stay limited to tool-specific loading notes when possible.

`CLAUDE.md` currently also carries the detailed build, run, test, and commit
reference. Use it together with this file for command-level details.

## Repo Layout

- `claw/`: Platform-independent core, services, shell, and Tool Use logic.
- `include/`: Public headers for OSAL, core, services, drivers, and platform
  abstractions.
- `osal/<rtos>/`: RTOS-specific OSAL implementations.
- `drivers/<subsystem>/<vendor>/`: Hardware drivers; public headers mirror
  this structure under `include/drivers/`.
- `platform/<board>/`: Board support packages and native build glue.
- `platform/common/<vendor>/`: Shared platform helpers.
- `scripts/`: Build, check, cross-file, QEMU, OTA, and test helper scripts.
- `tests/`: Unit and functional tests.
- `vendor/`: Third-party RTOS, BSP, and library submodules.
- `.agents/skills/`: Agent-agnostic workflows for developing rt-claw itself.

## Agent Skills

Skills live under `.agents/skills/<skill-name>/SKILL.md`. Agents that support
skills should load the matching skill before starting the task. Agents without
native skill support should read the corresponding `SKILL.md` file directly
and follow it as task-specific guidance.

Use these specialized skills for common tasks:

- `rt-claw-code-explorer`: Find subsystem ownership, call sites, build
  wiring, and OSAL/platform boundaries.
- `rt-claw-build`: Build rt-claw, choose platform targets, or debug compile
  and link failures.
- `rt-claw-testing`: Choose and run unit, smoke, functional, or pre-PR checks.
- `rt-claw-diagnose`: Analyze build logs, QEMU output, serial logs, network
  failures, CI output, and boot failures.
- `rt-claw-new-module`: Add a service, driver, tool, platform helper, or
  platform port following local structure.
- `rt-claw-platform-port`: Port rt-claw to a new board, SoC, or RTOS backend.
- `rt-claw-osal-review`: Review changes for OSAL boundary violations and RTOS
  coupling.
- `rt-claw-docs-sync`: Keep README and `docs/en` / `docs/zh` documentation in
  sync after behavior, API, platform, or command changes.
- `rt-claw-precommit`: Prepare a focused commit, check staged changes for
  secrets, run required checks, and enforce Signed-off-by rules.

Validate skill metadata with:

```bash
make check-agent-skills
```

## Core Rules

1. **OSAL boundary**: code in `claw/` must only include `claw_os.h` — never include FreeRTOS or RT-Thread headers directly.
2. **Minimal change**: modify only what is necessary. Do not refactor, add comments, or "improve" unrelated code.
3. **No backward compatibility**: prefer breaking changes over shims. Remove dead code, do not deprecate.
4. **Build before commit**: ensure the change compiles on at least one platform.
5. **Run checks**: `scripts/check-patch.sh --staged` must pass before committing.
6. **Validate skills**: `make check-agent-skills` must pass when `.agents/` changes.

## File Organization

- Platform-independent logic goes in `claw/`.
- RTOS-specific code goes in `osal/<rtos>/`.
- Hardware drivers go in `drivers/<subsystem>/<vendor>/`, headers in `include/drivers/<subsystem>/<vendor>/`.
- Platform BSP code goes in `platform/<board>/`.
- Shared platform helpers go in `platform/common/<vendor>/`.
- New services go in `claw/services/<name>/`.
- Never put application logic in `platform/` or `osal/`.

## Code Constraints

- C99 only. No C++ in core code.
- No dynamic memory allocation in OSAL layer — use static buffers or pool from `claw_config.h`.
- Config macros follow `CLAW_<SUBSYSTEM>_<PARAM>` naming in `include/claw/claw_config.h`.
- Every new `.c`/`.h` file needs the MIT SPDX license header.

## Commit Rules

- Format: `subsystem: description` — see CLAUDE.md for prefix list.
- Every commit must have `Signed-off-by: Chao Liu <chao.liu.zevorn@gmail.com>`.
- Do not add any AI/Claude co-author lines.
- One logical change per commit.

## Secrets & Credentials

- **Never commit** API keys, tokens, passwords, or any credentials to the repository.
- Secrets belong in `sdkconfig` (gitignored), environment variables, or local config files excluded by `.gitignore`.
- If a file contains secrets, ensure it is listed in `.gitignore` before staging.
- When reviewing staged changes (`git diff --cached`), check for hardcoded keys — reject the commit if any are found.

## Do NOT

- Add unit test frameworks, CI/CD pipelines, or automation without explicit request.
- Modify files under `vendor/` — these are git submodules.
- Use `//` comments in C code.
- Create `.md` documentation files unless explicitly asked.
- Guess or fabricate API behavior — read the source first.
- Commit files containing plaintext secrets (API keys, app IDs, tokens, etc.).
