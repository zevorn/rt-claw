# rt-claw Agent Skills

This directory contains agent-agnostic skills for working on rt-claw itself.
These are developer workflows for repository maintenance and firmware
development. They are separate from the runtime AI skill system implemented in
`claw/services/ai/ai_skill.c`.

The root `AGENTS.md` is the shared project guide and lists when to use each
skill. Agents that do not support native skill loading can read the matching
`SKILL.md` file directly.

Available skills:

- `rt-claw-code-explorer`: navigate the C/Meson/RTOS workspace and map
  subsystem ownership.
- `rt-claw-build`: choose and run repository build targets.
- `rt-claw-testing`: choose and run unit, smoke, functional, and pre-PR checks.
- `rt-claw-diagnose`: analyze build, boot, QEMU, serial, network, and CI
  failures.
- `rt-claw-new-module`: scaffold services, drivers, tools, platform helpers,
  and ports.
- `rt-claw-platform-port`: guide new board, SoC, or RTOS backend ports.
- `rt-claw-osal-review`: review OSAL boundary and RTOS coupling risks.
- `rt-claw-docs-sync`: synchronize README and bilingual docs after changes.
- `rt-claw-precommit`: prepare commits and PR-ready staged changes.

Validate skill metadata with:

```bash
make check-agent-skills
```
