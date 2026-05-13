---
name: rt-claw-osal-review
description: Use when reviewing rt-claw changes for OSAL boundary violations, RTOS coupling, platform leakage, driver layering, or portability risks.
license: MIT
---

# rt-claw OSAL Review

Review layering before style. The most important rule is that code in `claw/`
uses project abstractions and does not bind directly to an RTOS or vendor SDK.

## Review Checklist

1. Inspect changed includes:
   - `claw/` should include `claw_os.h` and project headers, not RTOS/vendor
     headers.
   - RTOS headers belong in `osal/<rtos>/`.
   - Board and SDK headers belong in `platform/` or hardware drivers.
2. Inspect ownership:
   - platform-independent logic -> `claw/`
   - RTOS-specific behavior -> `osal/<rtos>/`
   - board setup -> `platform/<board>/`
   - shared vendor helpers -> `platform/common/<vendor>/`
   - hardware access -> `drivers/<subsystem>/<vendor>/`
3. Inspect memory and concurrency:
   - no dynamic allocation in OSAL layer
   - static buffers or configured pools when OSAL state is needed
   - task, mutex, semaphore, timer, and KV behavior goes through OSAL APIs
4. Inspect configuration:
   - project config macros follow `CLAW_<SUBSYSTEM>_<PARAM>`
   - credentials must not be hardcoded
5. Inspect portability:
   - shared code should compile with FreeRTOS, RT-Thread, Zephyr, and Linux
     where enabled
   - use feature guards consistently with Meson options

## Output Format

Lead with findings ordered by severity. Include file and line references when
available. If no issues are found, say that clearly and mention remaining
validation gaps.

## Rules

- Do not request broad refactors for unrelated layering problems.
- Do not paper over RTOS leakage with wrapper macros in `claw/`.
- Do not add direct FreeRTOS, RT-Thread, Zephyr, ESP-IDF, or board includes to
  platform-independent code.
