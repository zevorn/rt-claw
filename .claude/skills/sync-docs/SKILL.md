---
name: sync-docs
description: Synchronize documentation after code changes. Use when user says "update docs", "sync documentation", "update README", "update architecture diagram", or after a significant feature/refactoring is complete.
disable-model-invocation: true
user-invocable: true
argument-hint: "[scope: all | readme | arch | usage | api]"
---

# Synchronize RT-Claw Documentation

Update documentation to reflect current codebase state. Always maintains bilingual EN/ZH parity.

## Arguments

$ARGUMENTS

Scope options:
- `all` (default): Full documentation sync
- `readme`: README.md and README_zh.md only
- `arch`: Architecture diagrams and docs/*/architecture.md
- `usage`: Build/run/flash usage guides
- `api`: API reference and header documentation

## Context

- Recent changes: !`git log --oneline -10`
- Changed files since last tag: !`git diff --name-only $(git describe --tags --abbrev=0 2>/dev/null || echo HEAD~20)..HEAD 2>/dev/null | head -40`
- Current docs structure: !`find docs/ -name '*.md' -type f 2>/dev/null | sort`
- README sections: !`grep '^##' README.md 2>/dev/null`

## Documentation Structure

```
README.md              (EN, primary)
README_zh.md           (ZH, mirror)
docs/
├── en/
│   ├── architecture.md
│   ├── coding-style.md
│   ├── usage.md
│   ├── porting.md
│   ├── tool-extension.md
│   └── tuning.md
└── zh/
    ├── architecture.md
    ├── coding-style.md
    ├── usage.md
    ├── porting.md
    ├── tool-extension.md
    └── tuning.md
```

## Workflow

### Step 1: Analyze what changed

Read the git diff to determine which subsystems were affected:
- `claw/services/*` → update architecture.md service descriptions
- `claw/tools/*` → update tool-extension.md
- `platform/*` → update usage.md build/run/flash sections
- `include/*` → update API references
- `osal/*` → update architecture.md OSAL section
- `drivers/*` → update architecture.md driver section
- `Makefile` / `meson_options.txt` → update usage.md build commands
- `claw_config.h` → update usage.md configuration section

### Step 2: Update affected documents

For each affected doc:

1. Read the current EN version
2. Identify sections that need updating
3. Update EN version with accurate, current information
4. Update ZH version to maintain parity (direct translation, not summary)

Rules:
- Keep ASCII architecture diagrams aligned (monospace box drawing)
- Feature tables must reflect actual implementation status
- Build/run commands must match current Makefile targets
- Configuration examples must match current option names and env vars
- Do NOT add content that describes unimplemented features

### Step 3: Update README if needed

If the scope includes README or the changes are significant:
- Update feature status table
- Update architecture diagram if subsystems changed
- Update Quick Start if build/run workflow changed
- Verify all internal links (`docs/en/...`, `docs/zh/...`) are valid

### Step 4: Verify consistency

- Check EN and ZH versions have matching section structure
- Check all referenced file paths exist
- Check all referenced Makefile targets exist
- Check code examples compile (if practical)

### Step 5: Summary

List all files updated and what was changed in each.
