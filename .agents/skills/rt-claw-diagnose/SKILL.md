---
name: rt-claw-diagnose
description: Use when analyzing rt-claw build logs, QEMU output, serial monitor logs, CI failures, network/API failures, or boot/runtime crashes.
license: MIT
---

# rt-claw Diagnose

Classify the failure first, then reduce it to the smallest command or code path
that proves the issue.

## Common Inputs

- Build logs from Meson, SCons, CMake, ESP-IDF, or Zephyr.
- QEMU boot output.
- Serial monitor logs from ESP32 hardware.
- Functional test or unit test output.
- Network, TLS, API proxy, Feishu, or Telegram errors.
- GitHub Actions or local CI output.

## Workflow

1. Identify the platform, command, branch, and changed files.
2. Classify the failure:
   - compile or link failure
   - boot failure
   - runtime crash or watchdog
   - QEMU/network/API failure
   - test assertion failure
   - style, DCO, or secret-check failure
3. Extract the first meaningful error, file, line, symbol, or backtrace frame.
4. Cross-check recent changes with `git diff` and relevant source files.
5. Form one primary hypothesis and the narrowest verification command.
6. Fix only the implicated subsystem, then rerun the narrowest useful check.
7. If uncertain, list two or three possible causes ranked by evidence and name
   the next command that would distinguish them.

## Common Patterns

| Pattern | Likely cause | Next check |
|---------|--------------|------------|
| `undefined reference to claw_*` | missing source in Meson or wrong feature guard | check `meson.build` includes the `.c` file |
| `implicit declaration of function` | missing include or wrong public header | check include order and exported headers |
| `redefinition of macro` | `claw_config.h` vs generated config conflict | check config guards and Meson options |
| `multiple definition of` | function body in header without `static inline` | move implementation to `.c` or mark inline |
| FreeRTOS/RT-Thread/Zephyr include inside `claw/` | OSAL boundary violation | move RTOS dependency to `osal/<rtos>/` |
| `DHCP timeout` or `dhcp_fine_tmr` | QEMU network setup issue | inspect QEMU `-nic` options |
| `DNS resolution failed` | no DNS in QEMU or wrong network config | use `scripts/api-proxy.py` when needed |
| `TLS handshake failed` or `esp_tls` | ESP32 QEMU TLS limitation | route through `scripts/api-proxy.py` |
| `HTTP 503` | API endpoint, rate limit, or token problem | verify endpoint and credentials outside commits |
| `Stack overflow` or `pxCurrentTCB` | task stack too small | inspect task creation stack sizes |
| `Data Abort` or `Prefetch Abort` | NULL pointer or bad memory access | rerun QEMU with `GDB=1` |
| `Guru Meditation` | ESP-IDF crash, watchdog, stack, or allocation issue | decode backtrace and inspect task state |
| watchdog timeout | blocking call, deadlock, or undersized task stack | check mutexes and long blocking calls |
| Feishu `no connection detected` | WebSocket endpoint or token issue | verify app ID/secret and endpoint |
| Telegram `HTTP 503` | bot token, API, or rate limit issue | verify with Telegram `getMe` |
| `connection reset by peer` | TLS version or proxy issue | check whether `api-proxy.py` is running |
| `QEMU boot test failed` | boot regression or network-dependent boot path | inspect boot banner and test profile |
| `scripts/check-patch.sh` failure | style, line length, comments, or whitespace | rerun staged style check |
| DCO failure | missing `Signed-off-by` | recommit with `git commit -s` |

## Rules

- Do not infer missing environment details. Mark them as missing.
- Do not commit credentials from logs, `sdkconfig`, environment dumps, or local
  config files.
- Do not broaden a fix to unrelated platforms without evidence.
- For bugs, add or update regression coverage when the current test structure
  can express the failure.
