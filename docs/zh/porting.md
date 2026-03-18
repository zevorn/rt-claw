# 移植与扩展指南

[English](../en/porting.md) | **中文**

## 移植到新平台

### 最低要求

- 32 位 MCU，SRAM ≥256KB
- 支持的 RTOS（FreeRTOS 或 RT-Thread），或愿意编写新的 OSAL 适配层
- 网络连接能力（以太网或 WiFi）
- 支持交叉编译的 C99 工具链

### 分步指南

#### 1. OSAL 实现

如果你的 RTOS 是 FreeRTOS 或 RT-Thread，可以直接复用现有的 `osal/` 代码。否则需要创建 `osal/<rtos>/claw_os_<rtos>.c`，实现 `include/osal/claw_os.h` 中的所有函数：

- **线程：** create、delete、delay、yield
- **互斥锁：** create、lock、unlock、delete
- **信号量：** create（二值/计数）、wait、post、delete
- **消息队列：** create、send、receive（带超时）、delete
- **定时器：** create（单次/重复）、start、stop、delete
- **内存：** malloc、calloc、free
- **日志：** `CLAW_LOGI` / `LOGW` / `LOGE` / `LOGD` 宏
- **时间：** `claw_os_get_tick_ms()`

同时需要实现 `osal/<rtos>/claw_net_<rtos>.c`，对应 `include/osal/claw_net.h`：

- `claw_http_post()` — 带请求头和请求体的 HTTP POST

#### 2. Meson 交叉编译文件

创建 `platform/<name>/cross.ini`（或通过脚本生成）：

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

#### 3. 平台目录

```
platform/<name>/
├── main.c              # 入口点：调用 claw_init()
├── boards/
│   └── <board>/
│       └── sdkconfig.defaults  # （ESP-IDF）或构建配置
├── cross.ini           # Meson 交叉编译文件
└── CMakeLists.txt      # 或 SConstruct（平台原生构建系统）
```

`main.c` 最小示例：

```c
#include "claw/claw_init.h"

int main(void)
{
    /* hardware init, network init */
    claw_init();
    /* start shell or block */
}
```

#### 4. 板级抽象（可选）

实现 `include/claw_board.h`：

```c
void board_early_init(void);  /* WiFi/GPIO init before claw_init */
const shell_cmd_t *board_platform_commands(int *count);  /* extra /commands */
```

#### 5. Makefile 集成

按照以下模式在根目录 `Makefile` 中添加构建目标：

```
make build-<name>-<board>
make run-<name>-<board>
make flash-<name>-<board>
```

#### 6. 构建与测试

```bash
meson setup build/<name>-<board>/meson --cross-file platform/<name>/cross.ini
meson compile -C build/<name>-<board>/meson
# Link with platform native build system
```

### 参考实现

| 模式 | 示例 | 关键文件 |
|------|------|----------|
| ESP-IDF (FreeRTOS) | `platform/esp32c3/` | CMakeLists.txt, components/rt_claw/, boards/ |
| 独立 FreeRTOS | `platform/zynq-a9/` | meson.build（完整固件）、FreeRTOSConfig.h、startup.S |
| RT-Thread (SCons) | `platform/vexpress-a9/` | SConstruct, rtconfig.py, cross.ini |

### 独立 FreeRTOS 模式（Zynq-A9 示例）

当移植到非 ESP-IDF 的 FreeRTOS 平台时，Zynq-A9 展示了"完整 Meson 构建"模式——
Meson 编译所有内容（内核 + BSP + OSAL + claw）生成单个 ELF 文件：

```
platform/zynq-a9/
├── startup.S               # 最小 ARM 启动代码（VFP、VBAR、栈设置）
├── FreeRTOS_asm_vectors.S   # IRQ/SWI 向量表
├── FreeRTOSConfig.h         # FreeRTOS 内核配置
├── FreeRTOSIPConfig.h       # FreeRTOS+TCP 网络配置
├── main.c                   # GIC + Timer 初始化、FreeRTOS+TCP 初始化、claw_init()
├── syscalls.c               # Newlib 桩函数（UART printf、sbrk、BSP 桩）
├── link.ld                  # 链接脚本
├── cross.ini                # Meson 交叉编译文件（osal='freertos', platform='zynq-a9'）
├── meson.build              # 编译 BSP + FreeRTOS + FreeRTOS+TCP + rtclaw → ELF
├── drivers/
│   └── NetworkInterface.c   # 修补的 Zynq GEM 网络驱动（QEMU 适配）
└── boards/qemu/
    └── board.c              # 板级抽象（QEMU 为空）
```

与 ESP-IDF 模式的关键差异：

- **无外部构建系统** — Meson 处理所有编译和链接
- **`platform` meson 选项** — 在 cross.ini 中设置 `platform = 'zynq-a9'`
  以选择 `CLAW_PLATFORM_FREERTOS`（而非 `CLAW_PLATFORM_ESP_IDF`）
- **BSP 在 vendor/ 中** — 硬件驱动（GIC、Timer、EMAC）位于 `vendor/bsp/xilinx/`
- **FreeRTOS+TCP** — 独立 TCP/IP 栈位于 `vendor/lib/freertos-plus-tcp/`
- **claw_net 使用 FreeRTOS+TCP socket** — `FreeRTOS_socket/connect/send/recv`
  替代 ESP-IDF 的 `esp_http_client` 或 POSIX socket

## 添加新工具

工具是在启动时注册的、可由 LLM 调用的函数。

### 1. 创建源文件

`claw/tools/tool_<name>.c`：

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

### 2. 在 claw_tools.c 中注册

在 `claw_tools_init()` 中调用 `claw_tools_register_my_tool()`，可选择使用 `#ifdef CONFIG_RTCLAW_TOOL_MY_TOOL` 条件编译保护。

### 3. 添加到构建系统

将源文件添加到 `claw/meson.build` 的 `src_files` 中。如果需要条件编译，添加对应的 meson 选项。

### 4. 添加到 Kconfig（ESP-IDF）

在 C3 和 S3 的 Kconfig 文件中，在"Tool Use"菜单下添加 `config RTCLAW_TOOL_MY_TOOL` 配置项。

## 添加新技能

技能是可复用的提示词模板。

### 内置技能（编译时）

在 `claw/services/ai/ai_skill.c` 的 `ai_skill_init()` 中添加：

```c
#ifdef CONFIG_RTCLAW_TOOL_SYSTEM
    ai_skill_register_builtin("health", "Full health report",
        "Run system_info, memory_info. Report CPU, memory, uptime. %s");
#endif
```

### 用户技能（运行时）

AI 可通过 `create_skill` 工具创建，用户也可通过未来的 `/skill create` 命令添加。技能持久化存储在 NVS Flash 中（命名空间 `claw_skill`），最多 16 个。

## 添加新驱动

遵循 Linux 内核风格的目录布局：

```
drivers/<subsystem>/<vendor>/<driver>.c
include/drivers/<subsystem>/<vendor>/<driver>.h
```

驱动源码应使用 `#ifdef CLAW_PLATFORM_ESP_IDF` / `#else` 桩函数模式以保证可移植性。将源文件添加到 `claw/meson.build` 的驱动部分。

每个驱动文件控制在 250 行以内。
