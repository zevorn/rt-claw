# Porting & Extension Guide

**English** | [中文](../zh/porting.md)

## Porting to a New Platform

### Minimum Requirements

- 32-bit MCU with ≥256KB SRAM
- A supported RTOS (FreeRTOS, RT-Thread, or Zephyr) or willingness to write a new OSAL
- Network connectivity (Ethernet or WiFi)
- C99 toolchain with cross-compilation support

### Step-by-Step

#### 1. OSAL Implementation

If your RTOS is FreeRTOS, RT-Thread, or Zephyr, reuse existing `osal/` code.
Otherwise create `osal/<rtos>/claw_os_<rtos>.c` implementing all functions in
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
#include "claw/init.h"

int main(void)
{
    /* hardware init, network init */
    claw_init();
    /* start shell or block */
}
```

#### 4. Board Abstraction (Optional)

Implement `include/platform/board.h`:

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
| Zephyr (CMake/west) | `platform/zephyr/` | CMakeLists.txt, prj.conf, boards/ |

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

### Zephyr Pattern

Zephyr uses CMake-native compilation -- OSAL and claw sources are compiled
directly by the Zephyr CMake build system rather than linked as Meson-prebuilt
static libraries. This avoids cross-build-system linking issues and integrates
cleanly with Zephyr's Kconfig and devicetree infrastructure.

OSAL backend: `osal/zephyr/` -- kernel primitives map to Zephyr kernel API
(`k_thread`, `k_mutex`, `k_sem`, `k_msgq`, `k_timer`). Network uses the
Zephyr HTTP Client with mbedTLS for native HTTPS. KV storage uses the Zephyr
Settings subsystem backed by NVS.

Key differences from other patterns:

- **No Meson prebuilt `.a`** -- Zephyr CMake compiles all sources directly
- **`west build`** -- standard Zephyr build command instead of `meson compile`
- **prj.conf** -- Kconfig-based configuration (kernel, networking, TLS)
- **Native HTTPS** -- mbedTLS integrated via Zephyr, no proxy required

## Adding a New Tool

Tools are LLM-callable functions registered at startup.

### 1. Create Source File

`claw/services/tools/<name>.c`:

```c
#include "claw/services/tools/tools.h"
#include "claw_config.h"

static claw_err_t my_tool_execute(struct claw_tool *tool,
                                  const cJSON *params, cJSON *result)
{
    (void)tool;
    /* extract parameters from params, do work, fill result */
    cJSON_AddStringToObject(result, "status", "ok");
    return CLAW_OK;
}

static const struct claw_tool_ops my_tool_ops = {
    .execute = my_tool_execute,
};

static struct claw_tool my_tool_def = {
    .name = "my_tool",
    .description = "Brief description for LLM",
    .input_schema_json =
        "{\"type\":\"object\",\"properties\":"
        "{\"param1\":{\"type\":\"string\"}},"
        "\"required\":[\"param1\"]}",
    .ops = &my_tool_ops,
};

#ifdef CONFIG_RTCLAW_TOOL_MY_TOOL
CLAW_TOOL_REGISTER(my_tool, &my_tool_def);
#endif
```

### 2. Auto-registration

Tools register via `CLAW_TOOL_REGISTER()` at link time (GNU ld linker sections)
or constructor time (ESP-IDF). No manual calls needed — guard with
`#ifdef CONFIG_RTCLAW_TOOL_MY_TOOL` to make registration conditional.

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
