<p align="center">
  <img src="images/logo.png" alt="RT-Claw" width="500">
</p>

<p align="center">
  <strong>让 AI 助理触手可及</strong>
</p>

<p align="center">
  <a href="https://discord.gg/gcxwYXQr"><img src="https://img.shields.io/badge/Discord-RT--Claw-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Discord"></a>
  <a href="https://qm.qq.com/q/heSPPC9De8"><img src="https://img.shields.io/badge/Join%20QQ-GTOC-brightgreen?style=for-the-badge&logo=QQ&logoColor=76bad9&color=76bad9" alt="QQ 群"></a>
  <a href="https://t.me/gevico_channel"><img src="https://img.shields.io/badge/Telegram-GTOC-blue?style=for-the-badge&logo=telegram" alt="Telegram"></a>
  <a href="https://space.bilibili.com/483048140"><img src="https://img.shields.io/badge/Bilibili-%E7%BB%9D%E5%AF%B9%E6%98%AF%E6%B3%BD%E6%96%87%E5%95%A6-FB7299?style=for-the-badge&logo=bilibili" alt="Bilibili"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg?style=for-the-badge" alt="MIT License"></a>
</p>

<p align="center"><strong>中文</strong> | <a href="README.md">English</a></p>

**RT-Claw** — 受 [OpenClaw](https://github.com/openclaw/openclaw) 启发，面向嵌入式设备的智能助手。
通过 OSAL 支持多 OS（FreeRTOS、RT-Thread、Linux），以组网节点构建蜂群智能。
ESP32-S3 WiFi 支持参考了 [MimiClaw](https://github.com/memovai/mimiclaw)。

> 仅需一美元的硬件成本，即可快速部署你的专属 AI 助理——无缝融入工作与生活，高效连接物理世界。

<p align="center">
  <img src="images/demo.png" alt="RT-Claw 演示 — AI 通过 Tool Use 在 LCD 上绘图" width="700">
</p>

[快速开始](docs/zh/getting-started.md) · [使用指南](docs/zh/usage.md) · [架构设计](docs/zh/architecture.md) · [移植与扩展](docs/zh/porting.md) · [裁剪与优化](docs/zh/tuning.md) · [贡献指南](docs/zh/contributing.md)

## 功能特性

| 功能 | 描述 | 状态 |
|------|------|------|
| LLM 对话引擎 | 通过 HTTP 调用 LLM API 进行交互式对话 | 已完成 |
| Tool Use | LLM 驱动的函数调用，与硬件和服务交互；30+ 内置工具 | 已完成 |
| 技能系统 | 可组合的多工具工作流；AI 可创建、持久化并执行融合多个工具的技能 | 已完成 |
| 对话记忆 | 短期 RAM 环形缓冲 + 长期 NVS Flash 持久化存储；AI 可保存/删除/查询记忆 | 已完成 |
| 蜂群智能 | 节点发现、心跳检测、能力位图、跨节点远程工具调用 | 已完成 |
| 定时任务 | 定时触发任务执行与周期性自动化；AI 可创建/查看/删除任务 | 已完成 |
| 对话优先 Shell | UART 交互终端，支持插入模式编辑、Tab 补全、UTF-8；直接输入发送 AI 对话，/命令 执行系统操作 | 已完成 |
| OSAL | 一次编写，在 FreeRTOS、RT-Thread 和 Linux 上零修改运行 | 已完成 |
| Gateway | 服务间线程安全的消息路由 | 已完成 |
| 网络 | 以太网（QEMU）和 WiFi（真实硬件）；HTTP 客户端用于 API 调用 | 已完成 |
| IM 集成 | 飞书（Lark）WebSocket 长连接；计划中：钉钉、QQ、Telegram | 进行中 |
| Web 刷写与串口 | 浏览器端固件刷写（esptool-js）+ 串口终端（ANSI 彩色渲染） | 已完成 |
| 多模型 API | 支持主流 LLM API：Claude、GPT、Gemini、DeepSeek、GLM（智谱）、MiniMax、Grok、Moonshot（Kimi）、百川、通义千问、豆包、Llama（Ollama） | 计划中 |
| Web 配置页面 | 内置轻量 Web 页面，支持在线配置 API Key、选择模型、调整参数 | 计划中 |
| Claw 技能提供者 | 作为其他 Claw 的技能插件，赋予其感知和控制物理世界的能力 | 计划中 |

## 架构

```
+----------------------------------------------------------------+
|                      rt-claw Application                       |
|     gateway | net | swarm | ai_engine | shell | sched | im     |
+----------------------------------------------------------------+
|                       skills (AI Skills)                       |
|              (one skill composes multiple tools)               |
+----------------------------------------------------------------+
|                        tools (Tool Use)                        |
|    gpio | system | lcd | audio | http | scheduler | memory     |
+----------------------------------------------------------------+
|                     drivers (Hardware BSP)                     |
|       WiFi | ES8311 | SSD1306 | serial | LCD framebuffer       |
+----------------------------------------------------------------+
|                   osal/claw_os.h (OSAL API)                    |
+----------------+----------------------+--------------+---------+
| FreeRTOS (IDF) | FreeRTOS(standalone) |  RT-Thread   |  Linux  |
+----------------+----------------------+--------------+---------+
| ESP32-C3 / S3  |  QEMU Zynq-A9 (GEM)  | vexpress-a9  |  Native |
+----------------+----------------------+--------------+---------+
```

## 支持平台

| 平台 | 运行目标 | OS | 构建系统 | 状态 |
|------|---------|-----|---------|------|
| ESP32-C3 | QEMU、xiaozhi-xmini、generic devkit | FreeRTOS (ESP-IDF) | Meson + CMake | 已验证 |
| ESP32-S3 | QEMU、真实硬件 | FreeRTOS (ESP-IDF) | Meson + CMake | 已验证 |
| Zynq-A9 | QEMU | FreeRTOS (standalone) | Meson（完整固件） | 已验证 |
| vexpress-a9 | QEMU | RT-Thread | Meson + SCons | 已验证 |
| Linux | 原生（x86_64、aarch64） | Linux (pthreads) | Meson | 已验证 |

## 快速开始

```bash
# 1. 一键安装 ESP-IDF + QEMU
./scripts/setup-esp-env.sh

# 2. 设置 API 密钥
export RTCLAW_AI_API_KEY='<你的 API 密钥>'

# 3. 编译并在 QEMU 上运行
make build-esp32c3-qemu
make run-esp32c3-qemu
```

> **没有硬件？没关系。** 试试 [CNB 云原生开发环境](https://cnb.cool/gevico.online/rtclaw/rt-claw)——所有工具链已预装，在浏览器中即可编译运行。

真实硬件（ESP32-S3/C3）、WiFi 配置、飞书机器人等详细步骤请参阅 **[快速开始指南](docs/zh/getting-started.md)**。

## 社区

加入 GTOC（格维开源社区）交流频道：

- **Discord**：[RT-Claw](https://discord.gg/gcxwYXQr)
- **QQ 群**：[加入](https://qm.qq.com/q/heSPPC9De8)
- **Telegram**：[GTOC 频道](https://t.me/gevico_channel)
- **Bilibili**：[绝对是泽文啦](https://space.bilibili.com/483048140)
- **微信**：[GTOC 微信公众号](https://mp.weixin.qq.com/s/PhTZKjk4FO0iVveBB9OvSQ)

## 致谢

受 [OpenClaw](https://github.com/openclaw/openclaw)、[Nanobot](https://github.com/HKUDS/nanobot) 和 [MimiClaw](https://github.com/memovai/mimiclaw) 启发。

## Star History

<a href="https://www.star-history.com/?repos=zevorn%2Frt-claw&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/image?repos=zevorn/rt-claw&type=date&theme=dark&legend=top-left" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/image?repos=zevorn/rt-claw&type=date&legend=top-left" />
   <img alt="Star History Chart" src="https://api.star-history.com/image?repos=zevorn/rt-claw&type=date&legend=top-left" />
 </picture>
</a>

## 许可证

MIT
