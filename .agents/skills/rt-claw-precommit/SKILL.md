---
name: rt-claw-precommit
description: Use when committing or opening a PR for rt-claw to review staged changes, detect secrets, run checks, and enforce commit conventions.
license: MIT
---

# rt-claw Precommit

Prepare one focused, signed commit. Never stage unrelated local changes
silently.

## Workflow

1. Inspect branch and status:
   - `git status -sb`
   - `git diff --stat`
   - `git diff --cached --stat`
2. Confirm which files belong to the commit when the worktree is mixed.
3. Review staged content:
   - `git diff --cached`
4. Check for hardcoded credentials:
   - API keys, app IDs, tokens, passwords, private URLs, and local secrets
   - `sdkconfig` and local config files that should remain ignored
5. Run required checks:
   - `scripts/check-patch.sh --staged`
   - `make check-agent-skills` when `.agents/` changed
   - at least one relevant build or test target for the changed area
6. Commit with the repository convention:
   - subject format: `subsystem: description`
   - include `Signed-off-by: Chao Liu <chao.liu.zevorn@gmail.com>`
   - do not include AI or Claude co-author lines

## Validation Choices

| Change area | Narrow validation |
|-------------|-------------------|
| docs or agent guidance | `scripts/check-patch.sh --staged` if source files are staged; otherwise manual diff review |
| `.agents/` | `make check-agent-skills` |
| shared `claw/` or `include/` | `make build-linux` and relevant unit tests |
| OSAL | build affected backend and at least one shared target |
| platform | platform-specific build target |
| tests | run the changed or narrowest affected test |

## Rules

- Do not use `git add -A` when unrelated changes are present.
- Do not commit build artifacts from `build/`.
- Do not commit changes under `vendor/`.
- Do not skip failed checks without reporting why.
