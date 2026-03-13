# Architecture Review & Optimization Plan

**English** | [中文](../zh/architecture-review.md)

Date: 2026-03-13
Scope: Full codebase review (OSAL, Core, Services, Tools, Platform)

## Executive Summary

The rt-claw architecture is well-structured with a clean OSAL abstraction
pattern. However, the review identified 10 issues across three severity
levels: 2 bugs (P0), 2 code hygiene issues (P1), and 6 design/architecture
improvements (P2). This document describes each issue, its impact, and
the planned fix.

## Issue Inventory

### P0 — Bugs (Fix Immediately)

#### P0-1: FreeRTOS Timer Callback Type Mismatch

**File:** `osal/freertos/claw_os_freertos.c:190-194`

**Problem:** FreeRTOS timer callbacks have signature
`void cb(TimerHandle_t xTimer)`, but the OSAL declares
`void (*callback)(void *arg)`. The current code force-casts the user
callback pointer:

```c
TimerHandle_t t = xTimerCreate(name, pdMS_TO_TICKS(period_ms),
                                repeat ? pdTRUE : pdFALSE,
                                arg,
                                (TimerCallbackFunction_t)callback);
```

The user's `arg` is stored as `pvTimerID`, but the callback receives the
`TimerHandle_t` — not `arg`. All current callers ignore their parameter
(`(void)arg`), so this works by accident. Any future timer callback that
uses `arg` will crash or read garbage.

**Fix:** Add a trampoline structure that stores both the user callback and
arg, retrieve them via `pvTimerGetTimerID()` in the trampoline.

**Risk:** Low — purely internal refactor, ABI unchanged.

#### P0-2: RT-Thread MQ Send Redundant Branch

**File:** `osal/rtthread/claw_os_rtthread.c:128-131`

**Problem:** Both `if` and `else` branches are identical:

```c
if (timeout_ms == CLAW_NO_WAIT || timeout_ms == CLAW_WAIT_FOREVER)
    ret = rt_mq_send_wait(..., ms_to_tick(timeout_ms));
else
    ret = rt_mq_send_wait(..., ms_to_tick(timeout_ms));
```

**Fix:** Remove the dead branch, keep single call.

---

### P1 — Code Hygiene (Fix Soon)

#### P1-1: Header Guard Naming Inconsistency

**Standard (per coding-style.md):** `CLAW_<PATH>_<NAME>_H`

**Violations:** Multiple headers use `__CLAW_*_H__` (double underscore
prefix is reserved by the C standard for the implementation):

| File | Current | Should Be |
|------|---------|-----------|
| `include/claw_os.h` | `__CLAW_OS_H__` | `CLAW_OS_H` |
| `include/claw_init.h` | `__CLAW_INIT_H__` | `CLAW_INIT_H` |
| `include/core/gateway.h` | `__CLAW_GATEWAY_H__` | `CLAW_CORE_GATEWAY_H` |
| `include/services/net/net_service.h` | `__CLAW_NET_SERVICE_H__` | `CLAW_SERVICES_NET_SERVICE_H` |
| `include/services/swarm/swarm.h` | `__CLAW_SWARM_H__` | `CLAW_SERVICES_SWARM_H` |
| `include/services/ai/ai_engine.h` | `__CLAW_AI_ENGINE_H__` | `CLAW_SERVICES_AI_ENGINE_H` |
| `include/tools/claw_tools.h` | `__CLAW_TOOLS_H__` | `CLAW_TOOLS_H` |

**Fix:** Rename all header guards to match the coding standard.

#### P1-2: Logging Disabled at Boot

**Problem:** `s_log_enabled` defaults to 0 in both OSAL implementations.
`claw_log_raw()` bypasses this check (the boot banner works), but all
`CLAW_LOGI`/`CLAW_LOGW`/`CLAW_LOGE` calls during early init are silently
dropped until the platform `main()` calls `claw_log_set_enabled(1)`.

**Fix:** Default `s_log_enabled` to 1. Logging should be on by default;
callers who want silence can explicitly disable it.

---

### P2 — Architecture Improvements (Plan & Discuss)

#### P2-1: OSAL Network Abstraction Missing

**Problem:** `claw_os.h` covers threads, sync, memory, logging, and time
— but not networking. All network-dependent code in `claw/` uses `#ifdef`
platform switches:

- `ai_engine.c` — ~350 lines of platform-specific HTTP transport
- `net_service.c` — ~240 lines, completely different per platform
- `swarm.c` — platform-specific node ID generation and socket includes

This undermines the OSAL's goal: "all core code depends only on
`include/claw_os.h`".

**Recommended approach:** Add a minimal HTTP client API to OSAL:

```c
int claw_http_post(const char *url, const char *headers[],
                   const char *body, size_t body_len,
                   char *resp, size_t resp_size);
```

Implement per-platform in `osal/freertos/` and `osal/rtthread/`.

#### P2-2: Gateway Is a No-Op Router ✅

**Problem:** `gateway.c` receives messages via its queue but only logs
them. No handler registration, no dispatch table, no subscriber pattern.
Services bypass it completely — `feishu.c` calls `ai_chat()` directly.
The `dst_channel` field in `struct gateway_msg` is unused.

**Resolution:** Adopted Option 2 — gateway is now a lightweight event
bus with `gateway_subscribe(type, handler, arg)` for handler registration
and type-based dispatch. Removed unused `src_channel` / `dst_channel`
fields. `CLAW_GW_MAX_HANDLERS` moved to `claw_config.h` for per-platform
tuning. Static handler table (no heap allocation) suits
resource-constrained devices.

#### P2-3: No Unified Service Interface

**Problem:** Services have inconsistent lifecycle patterns. Some have
`init()` only, some have `init()` + `start()`, none have `stop()` or
`deinit()`. The boot sequence in `claw_init()` is hardcoded.

**Recommendation:** Define a service descriptor:

```c
struct claw_service {
    const char *name;
    int (*init)(void);
    int (*start)(void);
    void (*stop)(void);
};
```

Register services in a table, iterate during init/shutdown.

#### P2-4: Blocking AI Call at Boot

**Problem:** `claw_init()` (line 68-82) makes a synchronous
`ai_chat_raw()` call during startup. If the API is unreachable, this
blocks boot for up to ~18 seconds (3 retries with exponential backoff).

**Fix:** Move the AI connectivity test to a scheduler task or a
low-priority thread that runs after boot completes.

#### P2-5: Fixed-Size Gateway Message Payload

**Problem:** `struct gateway_msg` embeds `uint8_t payload[256]` — every
message costs ~268 bytes regardless of actual payload size. With
`CLAW_GW_MSG_POOL_SIZE=16`, the queue alone uses ~4.3KB.

**Options:**
- Use a pointer + length (heap-allocated payload)
- Use a union with small inline buffer + overflow pointer
- Accept current design (adequate for ESP32-C3's 400KB SRAM)

**Recommendation:** Accept for now; revisit if memory pressure increases.

#### P2-6: Tool Registry Thread Safety

**Problem:** `s_tools[]` and `s_tool_count` in `claw_tools.c` have no
mutex protection. Currently safe because registration happens at init
time, but reads (`claw_tool_find`, `claw_tools_to_json`) occur from
multiple threads during runtime.

**Recommendation:** Low risk currently. Add a read lock if dynamic tool
registration is added later.

## Implementation Order

| Phase | Issues | Scope |
|-------|--------|-------|
| Phase 1 | P0-1, P0-2, P1-1, P1-2 | Bug fixes + code hygiene |
| Phase 2 | P2-4, P2-2 | Boot optimization + Gateway redesign |
| Phase 3 | P2-1, P2-3 | OSAL network + service interface |
