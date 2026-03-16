---
name: diagnose
description: Analyze error logs from QEMU output, serial monitor, CI pipelines, or build failures. This skill should automatically activate when the user pastes terminal output containing error messages, stack traces, build failures, boot logs, or CI job output.
user-invocable: true
argument-hint: "[paste error log or describe the issue]"
---

# Diagnose RT-Claw Errors

Analyze error output from various sources and suggest fixes.

## Arguments

$ARGUMENTS

The user typically pastes raw log output directly. Accept any format.

## Context

- Current platform config: !`cat build/*/meson/meson-info/intro-buildoptions.json 2>/dev/null | head -5 || echo "no build dir"`
- Recent changes: !`git diff --stat HEAD~3..HEAD 2>/dev/null`

## Common Error Patterns in RT-Claw

### Build Errors
| Pattern | Likely Cause | Fix |
|---------|-------------|-----|
| `undefined reference to claw_*` | Missing OSAL impl or Meson source list | Check meson.build includes the .c file |
| `implicit declaration of function` | Missing `#include` | Add the header to include order |
| `redefinition of macro` | `claw_config.h` vs `claw_gen_config.h` conflict | Check macro guard in claw_config.h |
| `multiple definition of` | Header has function body without `static inline` | Move to .c or add `static inline` |

### Runtime Errors (QEMU / Real Hardware)
| Pattern | Likely Cause | Fix |
|---------|-------------|-----|
| `DHCP timeout` / `dhcp_fine_tmr` | QEMU network not bridged | Check QEMU -nic option, use OpenCores Ethernet for ESP32 |
| `DNS resolution failed` | No DNS in QEMU or wrong network config | Use api-proxy.py for HTTP->HTTPS |
| `TLS handshake failed` / `esp_tls` | ESP32 QEMU lacks TLS offload | Route through api-proxy.py |
| `HTTP 503` | API endpoint down or rate limited | Check API URL and key validity |
| `Stack overflow` / `pxCurrentTCB` | Task stack too small | Increase stack size in task creation |
| `Data Abort` / `Prefetch Abort` | NULL pointer or bad memory access | Use GDB: `make run-*-qemu GDB=1` |
| `guru meditation error` | ESP-IDF crash (watchdog, stack, alloc) | Check backtrace, increase stack |
| `Task watchdog got triggered` | Task blocked too long | Check mutex deadlocks, reduce blocking calls |

### CI Errors
| Pattern | Likely Cause | Fix |
|---------|-------------|-----|
| `QEMU boot test failed` | AI boot test needs network (unavailable in CI) | Guard with `CONFIG_RTCLAW_AI_BOOT_TEST` |
| `check-patch.sh failed` | Code style violation | Run `scripts/check-patch.sh --staged` locally |
| `DCO check failed` | Missing `Signed-off-by` | Use `git commit -s` |

### Network/IM Errors
| Pattern | Likely Cause | Fix |
|---------|-------------|-----|
| Feishu `no connection detected` | Wrong WebSocket endpoint or token expired | Verify app_id/app_secret, check endpoint URL |
| Telegram `HTTP 503` | Bot token invalid or Telegram API down | Verify token with `getMe` API call |
| `connection reset by peer` | TLS version mismatch or proxy issue | Check api-proxy.py is running |

## Workflow

### Step 1: Classify the error

Determine the error category:
- **Build error**: compilation or linking failure
- **Runtime crash**: abort, stack overflow, watchdog, data abort
- **Network error**: DNS, TLS, HTTP, WebSocket
- **CI failure**: automated test or check failure
- **Boot failure**: system doesn't reach shell prompt

### Step 2: Extract key information

From the log output, identify:
- Error message and error code
- File and line number (if available)
- Call stack / backtrace
- Platform and configuration context

### Step 3: Root cause analysis

Cross-reference with:
1. The common error patterns table above
2. Recent code changes (`git diff`)
3. Platform-specific constraints (QEMU limitations, memory limits)
4. Known issues in the project

### Step 4: Suggest fix

Provide:
1. The most likely root cause
2. A concrete fix (code change, config change, or command)
3. How to verify the fix worked
4. If uncertain, list 2-3 possible causes ranked by probability

### Step 5: Offer to implement

Ask the user if they want the fix applied immediately.
Do NOT auto-apply — the user decides.
