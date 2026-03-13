# AGENTS.md

Rules for all AI agents working on rt-claw. Read CLAUDE.md first for build, style, and commit conventions.

## Core Rules

1. **OSAL boundary**: code in `src/` must only include `claw_os.h` — never include FreeRTOS or RT-Thread headers directly.
2. **Minimal change**: modify only what is necessary. Do not refactor, add comments, or "improve" unrelated code.
3. **No backward compatibility**: prefer breaking changes over shims. Remove dead code, do not deprecate.
4. **Build before commit**: ensure the change compiles on at least one platform.
5. **Run checks**: `scripts/check-patch.sh --staged` must pass before committing.

## File Organization

- Platform-independent logic goes in `src/`.
- RTOS-specific code goes in `osal/<rtos>/`.
- Platform BSP code goes in `platform/<board>/`.
- New services go in `src/services/<name>/`.
- Never put application logic in `platform/` or `osal/`.

## Code Constraints

- C99 only. No C++ in core code.
- No dynamic memory allocation in OSAL layer — use static buffers or pool from `claw_config.h`.
- Config macros follow `CLAW_<SUBSYSTEM>_<PARAM>` naming in `src/claw_config.h`.
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
