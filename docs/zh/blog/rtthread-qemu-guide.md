# 在 RT-Thread 上运行你的 AI 助理——RT-Claw QEMU vexpress-a9 实战指南

<p align="center">
  <img src="../../images/logo.png" alt="RT-Claw" width="500">
</p>

> 仅需一美元的硬件成本，即可快速部署你的专属 AI 助理——无缝融入工作与生活，高效连接物理世界。

## 前言

当我们谈论 AI 助手时，大多数人想到的是运行在云端服务器或手机上的应用。但如果 AI 能直接运行在一颗几块钱的 MCU 上呢？

**RT-Claw** 就是这样一个项目——它将 LLM（大语言模型）的对话能力和 Tool Use（函数调用）能力带到了嵌入式 RTOS 上。通过 OSAL（操作系统抽象层），RT-Claw 可以无缝运行在 **RT-Thread** 和 FreeRTOS 上，核心代码零修改。

本文将手把手带你在 **QEMU vexpress-a9** 上体验 RT-Claw + RT-Thread 的完整流程——无需任何硬件，只需一台有 Linux 开发环境的 PC 或者使用下文提供的云原生开发环境。

## RT-Thread 简介

[RT-Thread](https://www.rt-thread.org/) 是一款由国内开发者主导的开源实时操作系统（RTOS），经过多年的发展，已成为全球主流的嵌入式操作系统之一。它具有以下核心特性：

- **极小内核**：最小仅需 3KB ROM、1.2KB RAM，适合资源受限的 MCU
- **组件丰富**：内置文件系统、网络协议栈（lwIP）、设备驱动框架、FinSH Shell 等
- **生态完善**：拥有超过 500 个软件包，覆盖 IoT、AI、通信等领域
- **多架构支持**：ARM Cortex-M/A/R、RISC-V、MIPS、x86 等
- **社区活跃**：GitHub 10000+ Star，全球开发者贡献

在 RT-Claw 中，率先支持了 RT-Thread，作为 vexpress-a9 QEMU 目标的 OS 运行平台，提供了线程管理、网络协议栈、串口驱动和 Shell 等基础设施。

## 为什么选择 QEMU？

对于嵌入式开发来说，硬件依赖是最大的门槛。QEMU 模拟器让你可以：

- **零成本上手**：不需要购买任何开发板
- **快速迭代**：编译后几秒钟就能启动和测试
- **稳定可复现**：不受硬件差异和接线问题影响
- **CI 友好**：可以集成到自动化测试流程中

RT-Claw 使用 QEMU 模拟 ARM vexpress-a9 平台，搭配 RT-Thread v5.2.2，通过虚拟以太网（lan9118）连接网络，让 AI 引擎可以调用云端 LLM API。

## 架构概览

```
+--------------------------------------------------------------+
|                     rt-claw Application                      |
|    gateway | net | swarm | ai_engine | shell | sched | im    |
+--------------------------------------------------------------+
|                      skills (AI Skills)                      |
|             (one skill composes multiple tools)              |
+--------------------------------------------------------------+
|                       tools (Tool Use)                       |
| gpio | system | lcd | audio | http | scheduler | memory      |
+--------------------------------------------------------------+
|                   osal/claw_os.h  (OSAL API)                 |
+--------------------------------------------------------------+
|                          RT-Thread                           |
+--------------------------------------------------------------+
|                       QEMU vexpress-a9                       |
+--------------------------------------------------------------+
```

核心设计思想：

- **OSAL（操作系统抽象层）**：`claw_os.h` 定义统一接口，`claw_os_rtthread.c` 实现 RT-Thread 版本。所有业务代码只依赖 OSAL，实现一次编写、多平台运行。
- **Tool Use**：将 GPIO、系统信息、HTTP 请求、调度器等硬件/服务能力暴露为 JSON Schema 描述的「工具」，LLM 通过函数调用来操控设备。
- **Gateway**：线程安全的消息路由总线，解耦各服务模块。

## 环境准备

### 系统依赖

**Ubuntu / Debian：**

```bash
sudo apt-get install -y git wget python3 python3-pip python3-venv \
    cmake ninja-build gcc-arm-none-eabi qemu-system-arm scons meson
```

**Arch Linux：**

```bash
sudo pacman -S --needed git wget python cmake ninja \
    arm-none-eabi-gcc arm-none-eabi-newlib \
    qemu-system-arm scons meson
```

> 注：不需要安装 ESP-IDF，vexpress-a9 目标使用 Meson + SCons 构建。

也可以在浏览器中零配置体验：打开 [CNB 云端 IDE](https://cnb.cool/gevico.online/rtclaw/rt-claw)，所有工具链已预装。

### 获取代码

```bash
git clone --recursive https://github.com/zevorn/rt-claw.git
cd rt-claw
```

## 配置 AI 凭证

RT-Claw 支持主流 LLM API（Claude、GPT、DeepSeek 等），通过环境变量配置：

```bash
export RTCLAW_AI_API_KEY='你的 API Key'
export RTCLAW_AI_API_URL='https://api.anthropic.com/v1/messages'
export RTCLAW_AI_MODEL='claude-sonnet-4-6'
```

你也可以使用其他兼容 API，如 DeepSeek、通义千问等，只需修改 URL 和模型名称即可。

**优先级：** Meson 命令行选项 > 环境变量 > `claw_config.h` 默认值。

## 一键构建

```bash
make vexpress-a9-qemu
```

这条命令会自动执行：

1. **Meson 配置**：读取环境变量，生成交叉编译配置，将 API Key 等写入 `claw_gen_config.h`
2. **Meson 编译**：交叉编译 `claw/`（业务代码）和 `osal/`（RT-Thread OSAL）为静态库
3. **SCons 编译**：编译 RT-Thread 内核 + BSP，链接 Meson 产出的静态库
4. **输出固件**：`build/vexpress-a9-qemu/rtthread.bin`

整个流程大约 30 秒（首次编译），增量编译仅需几秒。

## 启动 API 代理

这一步非常关键。RT-Thread 的 lwIP 网络协议栈不支持 TLS（HTTPS），但云端 LLM API 都是 HTTPS。RT-Claw 项目自带了一个轻量级 HTTP→HTTPS 代理脚本来解决这个问题：

```bash
python3 scripts/api-proxy.py https://api.anthropic.com &
```

代理启动后会监听 `0.0.0.0:8888`。QEMU 内部的固件通过 `http://10.0.2.2:8888` 访问代理（`10.0.2.2` 是 QEMU 用户模式网络中宿主机的地址），代理再将请求转发到真实的 HTTPS API。

```
+--------------+     HTTP     +--------------+    HTTPS     +--------------+
|  RT-Thread   | ------------ |  api-proxy   | ------------ |  Cloud LLM   |
|  (QEMU)      |  10.0.2.2:   |  (Host)      |              |  API         |
|              |     8888     |              |              |              |
+--------------+              +--------------+              +--------------+
```

## 运行

```bash
make run-vexpress-a9-qemu
```

启动后你会看到 RT-Thread 的启动日志：

```
 \ | /
- RT -     Thread Operating System
 / | \     5.2.2 build Mar 14 2026 18:13:45
 2006 - 2024 Copyright by RT-Thread team
lwIP-2.1.2 initialized!

  +-----------------------------------------+
  |          rt-claw v0.2.0                 |
  |  Real-Time Claw / Swarm Intelligence    |
  +-----------------------------------------+

[I/init] init: gateway
[I/gateway] initialized
[I/init] init: sched
[I/sched] started, max_tasks=8, tick=1000ms
[I/init] init: swarm
[I/swarm] initialized, self_id=0x52540028, max_nodes=32
[I/init] init: net
[I/net] waiting for DHCP ...
[I/net] got ip: 10.0.2.15
[I/net] network service ready
[I/init] init: tools
[I/tools] 10 tools registered
[I/init] init: ai_engine
[I/ai] engine ready (model: claude-sonnet-4-6, tools: 10)
  [boot] Testing AI connection ...
[I/ai] tool_use: system_info
  [boot] AI> rt-claw is online on QEMU vexpress-a9 (RT-Thread)
             and fully operational. ✅
```

启动过程中，RT-Claw 会自动执行一次 AI 连接测试——AI 会调用 `system_info` 工具获取设备信息，然后回复确认状态。这验证了整条链路的畅通：

**RT-Thread Shell → AI Engine → HTTP → API Proxy → Cloud LLM → Tool Use → 回复**

## 与 AI 对话

RT-Claw 采用「对话优先」的 Shell 设计。在终端中直接输入自然语言，就会发送给 AI：

```
msh />你好

  thinking ...

rt-claw> 你好！我是 RT-Claw，一个运行在 QEMU vexpress-a9 上的
嵌入式 AI 助手。我可以帮你：
- 查看系统信息和内存状态
- 创建定时任务
- 发送 HTTP 请求
- 管理记忆和技能

有什么需要帮忙的吗？
```

使用 `/` 前缀执行系统命令：

```
/help          # 查看所有命令
/ai_status     # 查看 AI 配置
/nodes         # 查看蜂群节点
/clear         # 清空对话
```

## Tool Use 实战

Tool Use 是 RT-Claw 最核心的能力——LLM 不只是聊天，它能通过函数调用来操控设备。

### 查看系统信息

```
查看一下当前的系统信息和内存状态
```

AI 会自动调用 `system_info` 和 `memory_info` 两个工具，然后汇总结果：

```
rt-claw> 系统信息：
- 项目：rt-claw v0.2.0
- 平台：QEMU vexpress-a9
- RTOS：RT-Thread 5.2.2
- 运行时间：约 45 秒

内存状态：
- 总堆内存：~8 MB
- 当前空闲：~6.5 MB
- 内存使用率：约 20%
```

### 创建定时任务

```
每 30 秒检查一次内存状态
```

AI 调用 `schedule_task` 工具：

```
[I/ai] tool_use: schedule_task
[I/sched] task 'mem_check' created, interval=30s, count=-1

rt-claw> 已创建定时任务 "mem_check"，每 30 秒检查一次内存状态。
```

### 发送 HTTP 请求

```
帮我请求一下 http://httpbin.org/get
```

AI 调用 `http_request` 工具发送 GET 请求并返回结果。

## 飞书机器人集成

RT-Claw 支持通过飞书（Lark）WebSocket 长连接与 AI 交互——无需公网 IP，设备启动后自动建立连接。下文使用 ESP32-C3 硬件演示。

<p align="center">
  <img src="../../images/feishu_talk1.png" alt="飞书对话 — AI 自我介绍" width="500">
</p>

在飞书中直接和机器人对话，它会调用设备上的工具来获取信息：

<p align="center">
  <img src="../../images/feishu_talk2.png" alt="飞书对话 — AI 查看系统信息" width="500">
</p>

配置方式：

```bash
export RTCLAW_FEISHU_APP_ID='cli_xxx'
export RTCLAW_FEISHU_APP_SECRET='xxx'
```

在[飞书开放平台](https://open.feishu.cn)创建应用，开启事件订阅长连接模式，订阅 `im.message.receive_v1` 事件即可。

## GDB 调试

所有 QEMU 目标都支持 GDB 调试：

```bash
# 终端 1 — 以调试模式启动
make run-vexpress-a9-qemu GDB=1

# 终端 2 — 连接 GDB
arm-none-eabi-gdb build/vexpress-a9-qemu/rtthread.elf \
    -ex 'target remote :1234'
```

启动时 CPU 会暂停在入口点，等待 GDB 连接。你可以设置断点、单步执行、查看变量——完整的嵌入式调试体验。

## OSAL 的设计哲学

RT-Claw 能同时支持 RT-Thread 和 FreeRTOS，关键在于 OSAL（操作系统抽象层）。

`include/osal/claw_os.h` 定义了统一接口：

```c
/* 线程 */
int claw_thread_create(claw_thread_t *t, const char *name,
                       void (*entry)(void *), void *arg,
                       uint32_t stack, uint32_t prio);

/* 互斥锁 */
int claw_mutex_create(claw_mutex_t *m);
int claw_mutex_lock(claw_mutex_t *m, uint32_t timeout_ms);
int claw_mutex_unlock(claw_mutex_t *m);

/* 消息队列 */
int claw_mq_create(claw_mq_t *mq, const char *name,
                   uint32_t msg_size, uint32_t capacity);

/* 网络 */
int claw_http_post(const char *url, const char *body,
                   const claw_http_header_t *headers, int hdr_cnt,
                   char *resp_buf, int resp_buf_size);
```

`osal/rtthread/claw_os_rtthread.c` 将这些接口映射到 RT-Thread API：

```c
int claw_thread_create(claw_thread_t *t, const char *name,
                       void (*entry)(void *), void *arg,
                       uint32_t stack, uint32_t prio)
{
    t->handle = rt_thread_create(name, entry, arg, stack, prio, 20);
    if (!t->handle) return -1;
    rt_thread_startup(t->handle);
    return 0;
}
```

所有业务代码（`claw/` 目录）只调用 `claw_os.h`，完全不感知底层是 RT-Thread 还是 FreeRTOS。新增 RTOS 支持只需实现一个新的 `claw_os_xxx.c` 文件。

## 项目特性一览

| 功能 | 描述 | 状态 |
|------|------|------|
| LLM 对话引擎 | 通过 HTTP 调用 LLM API 进行交互式对话 | ✅ |
| Tool Use | LLM 驱动的函数调用，30+ 内置工具 | ✅ |
| 技能系统 | 可组合的多工具工作流，NVS 持久化 | ✅ |
| 对话记忆 | 短期 RAM + 长期 Flash 持久化 | ✅ |
| 蜂群智能 | 节点发现、心跳、远程工具调用 | ✅ |
| 定时任务 | AI 可创建周期性自动化任务 | ✅ |
| OSAL | RT-Thread + FreeRTOS 双平台 | ✅ |
| IM 集成 | 飞书 WebSocket + Telegram Bot | ✅ |
| Web 刷写 | 浏览器端固件烧录 + ANSI 串口终端 | ✅ |

## 快速体验

完整操作仅需 5 条命令：

```bash
# 1. 克隆代码
git clone --recursive https://github.com/zevorn/rt-claw.git
cd rt-claw

# 2. 设置 API Key
export RTCLAW_AI_API_KEY='你的 API Key'

# 3. 构建
make vexpress-a9-qemu

# 4. 启动代理
python3 scripts/api-proxy.py https://api.anthropic.com &

# 5. 运行
make run-vexpress-a9-qemu
```

## 写在最后

RT-Claw 还是一个很年轻的项目，但我们相信嵌入式 AI 的未来就在边缘——不需要昂贵的服务器，不需要复杂的部署，一颗几块钱的芯片就能成为你的智能助手。

如果这篇文章对你有启发，或者你觉得 RT-Claw 的方向有意思，欢迎到 GitHub 给我们点个 **Star**，这是对开源项目最大的支持和鼓励：

**https://github.com/zevorn/rt-claw**

我们也非常欢迎各种形式的参与——提 Issue、提 PR、分享给朋友，或者只是在社区里聊聊你的想法。

### 社区

- **GitHub**：[zevorn/rt-claw](https://github.com/zevorn/rt-claw)
- **官网**：[rt-claw.com](https://www.rt-claw.com)
- **Discord**：[RT-Claw](https://discord.gg/gcxwYXQr)
- **QQ 群**：[加入 GTOC](https://qm.qq.com/q/heSPPC9De8)
- **Telegram**：[GTOC 频道](https://t.me/gevico_channel)

格维开源社区（GTOC）—— 格物致知，多维创新。

---

*本文由 RT-Claw 项目作者泽文撰写，感谢 RT-Thread 社区提供的优秀实时操作系统基础设施。*
