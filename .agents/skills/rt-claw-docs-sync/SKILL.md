---
name: rt-claw-docs-sync
description: Use when rt-claw behavior, APIs, configuration, build commands, platforms, or developer workflows change and documentation may need synchronization.
license: MIT
---

# rt-claw Docs Sync

Keep documentation accurate and bilingual where the repository already has
English and Chinese pairs.

## Documentation Map

| Path | Purpose |
|------|---------|
| `README.md` | English project overview |
| `README_zh.md` | Chinese project overview |
| `docs/en/` | English developer and user documentation |
| `docs/zh/` | Chinese counterpart documentation |
| `CLAUDE.md` | Command-level agent reference |
| `AGENTS.md` | Shared agent instruction source |
| `.agents/` | Agent-agnostic development skills |

Common scopes:

- `readme`: `README.md` and `README_zh.md`.
- `arch`: architecture docs and diagrams.
- `usage`: build, run, flash, and shell usage guides.
- `api`: API references and header documentation.
- `agent`: `AGENTS.md`, `CLAUDE.md`, `GEMINI.md`, Copilot instructions, and
  `.agents/`.

## Workflow

1. Read the diff and identify changed behavior, APIs, commands, platforms, or
   configuration.
2. Update only documentation that the change actually affects.
3. Preserve EN/ZH parity when changing paired docs.
4. Keep command examples aligned with `Makefile`, `meson_options.txt`, and
   current scripts.
5. Do not document unimplemented features as done.
6. Verify referenced paths and commands exist.
7. For paired EN/ZH docs, update both sides in the same change unless there is
   an explicit reason not to.

## Change Triggers

- `claw/services/*` -> architecture, usage, and service docs may need updates.
- `claw/services/tools/*` -> Tool Use and usage docs may need updates.
- `include/*` -> API references may need updates.
- `osal/*` -> architecture and porting docs may need updates.
- `platform/*` -> getting-started, usage, and platform docs may need updates.
- `Makefile`, `meson_options.txt`, or `scripts/*` -> build and usage docs may
  need updates.
- `.agents/*`, `AGENTS.md`, or tool-specific instruction files -> agent
  guidance needs updates.

## Rules

- Do not add broad roadmap text for a narrow code change.
- Do not change generated files or build artifacts.
- Do not create new standalone docs unless the task explicitly asks for them.
