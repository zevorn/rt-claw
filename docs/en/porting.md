# Porting & Extension Guide

**English** | [中文](../zh/porting.md)

## Porting to a New Platform

### Minimum Requirements

- 32-bit MCU with ≥256KB SRAM
- A supported RTOS (FreeRTOS or RT-Thread) or willingness to write a new OSAL
- Network connectivity (Ethernet or WiFi)
- C99 toolchain with cross-compilation support

### Step-by-Step

#### 1. OSAL Implementation

If your RTOS is FreeRTOS or RT-Thread, reuse existing `osal/` code. Otherwise
create `osal/<rtos>/claw_os_<rtos>.c` implementing all functions in
`include/osal/claw_os.h`:

- **Thread:** create, delete, delay, yield
- **Mutex:** create, lock, unlock, delete
- **Semaphore:** create (binary/counting), wait, post, delete
- **Message Queue:** create, send, receive (with timeout), delete
- **Timer:** create (oneshot/repeat), start, stop, delete
- **Memory:** malloc, calloc, free
- **Logging:** `CLAW_LOGI` / `LOGW` / `LOGE` / `LOGD` macros
- **Time:** `claw_os_get_tick_ms()`

Also implement `osal/<rtos>/claw_net_<rtos>.c` for `include/osal/claw_net.h`:

- `claw_http_post()` — HTTP POST with headers and body

#### 2. Meson Cross-File

Create `platform/<name>/cross.ini` (or generate via script):

```ini
[binaries]
c = 'arm-xxx-gcc'
ar = 'arm-xxx-ar'
strip = 'arm-xxx-strip'

[host_machine]
system = 'none'
cpu_family = 'arm'
cpu = 'cortex-m4'
endian = 'little'

[built-in options]
c_args = ['-march=...', '-DCLAW_PLATFORM_CUSTOM']

[project options]
osal = 'freertos'
```

#### 3. Platform Directory

```
platform/<name>/
├── main.c              # Entry point: call claw_init()
├── boards/
│   └── <board>/
│       └── sdkconfig.defaults  # (ESP-IDF) or build config
├── cross.ini           # Meson cross-file
└── CMakeLists.txt      # or SConstruct (platform native build)
```

`main.c` minimal:

```c
#include "claw/claw_init.h"

int main(void)
{
    /* hardware init, network init */
    claw_init();
    /* start shell or block */
}
```

#### 4. Board Abstraction (Optional)

Implement `include/claw_board.h`:

```c
void board_early_init(void);  /* WiFi/GPIO init before claw_init */
const shell_cmd_t *board_platform_commands(int *count);  /* extra /commands */
```

#### 5. Makefile Integration

Add targets to root `Makefile` following the pattern:

```
make build-<name>-<board>
make run-<name>-<board>
make flash-<name>-<board>
```

#### 6. Build and Test

```bash
meson setup build/<name>-<board>/meson --cross-file platform/<name>/cross.ini
meson compile -C build/<name>-<board>/meson
# Link with platform native build system
```

### Reference Implementations

| Pattern | Example | Key Files |
|---------|---------|-----------|
| ESP-IDF (FreeRTOS) | `platform/esp32c3/` | CMakeLists.txt, components/rt_claw/, boards/ |
| Standalone FreeRTOS | `platform/zynq-a9/` | meson.build (full firmware), FreeRTOSConfig.h, startup.S |
| RT-Thread (SCons) | `platform/vexpress-a9/` | SConstruct, rtconfig.py, cross.ini |

### Standalone FreeRTOS Pattern (Zynq-A9 Example)

When porting to a non-ESP-IDF FreeRTOS platform, the Zynq-A9 port demonstrates
the "full Meson build" pattern where Meson compiles everything (kernel + BSP +
OSAL + claw) into a single ELF:

```
platform/zynq-a9/
├── startup.S               # Minimal ARM boot (VFP, VBAR, stacks)
├── FreeRTOS_asm_vectors.S   # IRQ/SWI vector table
├── FreeRTOSConfig.h         # FreeRTOS kernel configuration
├── FreeRTOSIPConfig.h       # FreeRTOS+TCP network configuration
├── main.c                   # GIC + Timer init, FreeRTOS+TCP init, claw_init()
├── syscalls.c               # Newlib stubs (UART printf, sbrk, BSP stubs)
├── link.ld                  # Linker script
├── cross.ini                # Meson cross-file (osal='freertos', platform='zynq-a9')
├── meson.build              # Compiles BSP + FreeRTOS + FreeRTOS+TCP + rtclaw → ELF
├── drivers/
│   └── NetworkInterface.c   # Patched Zynq GEM driver (QEMU workarounds)
└── boards/qemu/
    └── board.c              # Board abstraction (empty for QEMU)
```

Key differences from ESP-IDF pattern:

- **No external build system** — Meson handles everything
- **`platform` meson option** — set `platform = 'zynq-a9'` in cross.ini to
  select `CLAW_PLATFORM_FREERTOS` (vs `CLAW_PLATFORM_ESP_IDF`)
- **BSP in vendor/** — hardware drivers (GIC, Timer, EMAC) in `vendor/bsp/xilinx/`
- **FreeRTOS+TCP** — standalone TCP/IP stack in `vendor/lib/freertos-plus-tcp/`
- **claw_net uses FreeRTOS+TCP sockets** — `FreeRTOS_socket/connect/send/recv`
  instead of ESP-IDF `esp_http_client` or POSIX sockets

## Adding a New Tool

Tools are LLM-callable functions registered at startup.

### 1. Create Source File

`claw/tools/tool_<name>.c`:

```c
#include "osal/claw_os.h"
#include "claw_config.h"
#include "claw/tools/claw_tools.h"
#include <vendor/lib/cjson/cJSON.h>

static char *execute_my_tool(const char *params_json)
{
    cJSON *p = cJSON_Parse(params_json);
    /* extract parameters, do work */
    cJSON_Delete(p);
    return claw_os_strdup("{\"status\":\"ok\"}");  /* must be free-able */
}

void claw_tools_register_my_tool(void)
{
    claw_tool_register(
        "my_tool",
        "Brief description for LLM",
        "{\"type\":\"object\",\"properties\":{\"param1\":{\"type\":\"string\"}},\"required\":[\"param1\"]}",
        execute_my_tool,
        0, 0
    );
}
```

### 2. Register in claw_tools.c

Call `claw_tools_register_my_tool()` from `claw_tools_init()`, optionally behind
a `#ifdef CONFIG_RTCLAW_TOOL_MY_TOOL` guard.

### 3. Add to Build

Add source file to `claw/meson.build` `src_files`. Add meson option if
conditional.

### 4. Add to Kconfig (ESP-IDF)

Add `config RTCLAW_TOOL_MY_TOOL` entry under "Tool Use" menu in both C3 and S3
Kconfig files.

## Adding a New Skill

Skills are reusable prompt templates.

### Built-in Skill (compile-time)

In `claw/services/ai/ai_skill.c`, add to `ai_skill_init()`:

```c
#ifdef CONFIG_RTCLAW_TOOL_SYSTEM
    ai_skill_register_builtin("health", "Full health report",
        "Run system_info, memory_info. Report CPU, memory, uptime. %s");
#endif
```

### User Skill (runtime)

AI creates via `create_skill` tool, or user adds via future `/skill create`
command. Skills persist in NVS Flash (namespace `claw_skill`), max 16 total.

## Adding a New Driver

Follow Linux-kernel style layout:

```
drivers/<subsystem>/<vendor>/<driver>.c
include/drivers/<subsystem>/<vendor>/<driver>.h
```

Driver source should use `#ifdef CLAW_PLATFORM_ESP_IDF` / `#else` stubs pattern
for portability. Add to `claw/meson.build` driver section.

Keep under 250 lines.
