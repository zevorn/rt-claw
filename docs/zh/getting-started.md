# 快速入门

[English](../en/getting-started.md) | **中文**

本指南涵盖在所有支持平台上构建、烧录和运行 rt-claw 的完整流程，
包括 ESP32-C3、ESP32-S3、vexpress-a9（RT-Thread）、Zynq-A9（FreeRTOS）、
Zephyr（Cortex-A9 / Cortex-M3）和 Linux 原生平台。

## 前置条件

### 系统依赖

**Ubuntu / Debian：**

```bash
sudo apt-get install -y git wget flex bison gperf python3 python3-pip \
    python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util \
    libusb-1.0-0 gcc-arm-none-eabi qemu-system-arm scons meson \
    libcurl4-openssl-dev ca-certificates \
    libgcrypt20-dev libglib2.0-dev libpixman-1-dev libsdl2-dev libslirp-dev
```

**Arch Linux：**

```bash
sudo pacman -S --needed git wget flex bison python python-pip cmake ninja \
    ccache dfu-util libusb arm-none-eabi-gcc arm-none-eabi-newlib \
    qemu-system-arm scons meson curl ca-certificates \
    libgcrypt glib2 pixman sdl2 libslirp
```

### ESP-IDF + QEMU

所有 ESP32-C3 和 ESP32-S3 目标都需要此环境。安装脚本会自动安装
ESP-IDF v5.4、工具链以及 Espressif QEMU（riscv32 + xtensa）：

```bash
./scripts/setup-esp-env.sh
source $HOME/esp/esp-idf/export.sh    # 每次构建前都需要执行
```

### 仅 vexpress-a9

上述系统依赖中的 ARM QEMU 和 SCons 包即可满足需求，无需安装 ESP-IDF。

## 配置

### AI 凭证

通过环境变量（推荐）或 Meson 选项进行设置。

| 变量 | 用途 | 示例 |
|------|------|------|
| `RTCLAW_AI_API_KEY` | API 密钥 | `sk-ant-...` |
| `RTCLAW_AI_API_URL` | API 端点 | `https://api.anthropic.com/v1/messages` |
| `RTCLAW_AI_MODEL` | 模型名称 | `claude-sonnet-4-6` |

```bash
# 环境变量（写入 shell 配置文件或在构建前导出）
export RTCLAW_AI_API_KEY='sk-...'
export RTCLAW_AI_API_URL='https://api.anthropic.com/v1/messages'
export RTCLAW_AI_MODEL='claude-sonnet-4-6'

# 或直接传递给 Meson（优先级最高）
meson setup build/... --cross-file ... -Dai_api_key='sk-...' -Dai_api_url='...'
```

**优先级：** Meson 选项 > 环境变量 > `claw_config.h` 默认值。

### 飞书（Lark）即时通讯

使用飞书机器人网关的步骤：

1. 在 [open.feishu.cn](https://open.feishu.cn) 创建应用
2. 启用**事件订阅**并选择长连接模式
3. 订阅 `im.message.receive_v1` 事件
4. 导出凭证：

```bash
export RTCLAW_FEISHU_APP_ID='cli_...'
export RTCLAW_FEISHU_APP_SECRET='...'
```

或使用 Meson 选项：`-Dfeishu=true -Dfeishu_app_id=... -Dfeishu_app_secret=...`

### Telegram Bot

使用 Telegram 机器人集成的步骤：

1. 在 Telegram 中通过 [@BotFather](https://t.me/BotFather) 创建机器人
2. 复制 Bot Token（格式如 `123456:ABC-DEF...`）
3. 导出凭证：

```bash
export RTCLAW_TELEGRAM_BOT_TOKEN='123456:ABC-DEF...'
```

或使用 Meson 选项：`-Dtelegram=true -Dtelegram_bot_token='...'`

QEMU 测试时（无 TLS），需在单独端口启动 Telegram API 代理：

```bash
python3 scripts/api-proxy.py https://api.telegram.org 8889
```

并将 API URL 指向代理：

```bash
meson configure build/<platform>/meson \
    -Dtelegram=true \
    -Dtelegram_bot_token='...' \
    -Dtelegram_api_url='http://10.0.2.2:8889'
```

**真实硬件**（ESP32-C3/S3，带 WiFi）支持原生 TLS，无需代理：

```bash
export RTCLAW_TELEGRAM_BOT_TOKEN='123456:ABC-DEF...'

# ESP32-C3 xiaozhi-xmini 示例
make build-esp32c3-xiaozhi-xmini
make flash-esp32c3-xiaozhi-xmini
make run-esp32c3-xiaozhi-xmini    # 串口监视器 — 观察 [telegram] 日志

# ESP32-S3 示例
make esp32s3
make flash-esp32s3
make monitor-esp32s3
```

烧录后，通过 `/wifi_set <SSID> <PASS>` 配置 WiFi，然后重启。
设备连接 WiFi 后会自动开始轮询 Telegram，即可接收来自机器人的消息。

### WiFi（仅限硬件开发板）

构建前通过 `idf.py menuconfig` 配置，或在运行时设置：

```
/wifi_set <SSID> <PASSWORD>
```

## ESP32-C3

### QEMU（无需硬件）

QEMU 开发板附带三种配置预设：

| 预设 | 文件 | Shell | 飞书 | 使用场景 |
|------|------|-------|------|----------|
| 快速演示 | `sdkconfig.defaults.demo` | 开启 | 关闭 | 交互式 Shell + 完整 AI 代理 |
| 飞书机器人 | `sdkconfig.defaults.feishu` | 关闭 | 开启 | 无头 IM 机器人 |
| 默认 | `sdkconfig.defaults` | — | — | 最小化 / 自定义 |

切换预设时，在构建前复制所需的文件：

```bash
source $HOME/esp/esp-idf/export.sh

# 选择预设（可选 — 跳过则使用当前默认配置）
cp platform/esp32c3/boards/qemu/sdkconfig.defaults.demo \
   platform/esp32c3/boards/qemu/sdkconfig.defaults

# 构建并运行
make build-esp32c3-qemu
make run-esp32c3-qemu               # 仅串口
make run-esp32c3-qemu GRAPHICS=1    # 带 LCD 显示窗口
make run-esp32c3-qemu GDB=1         # 调试模式（GDB 端口 1234）
```

### devkit（4 MB，真实硬件）

通用 ESP32-C3 开发板，支持 WiFi。

```bash
source $HOME/esp/esp-idf/export.sh
make build-esp32c3-devkit
make flash-esp32c3-devkit
make run-esp32c3-devkit              # 串口监视器
```

### xiaozhi-xmini（16 MB，真实硬件）

小智 Mini 开发板，配备 16 MB Flash、WiFi 和 ES8311 音频编解码器。

```bash
source $HOME/esp/esp-idf/export.sh
make build-esp32c3-xiaozhi-xmini
make flash-esp32c3-xiaozhi-xmini
make run-esp32c3-xiaozhi-xmini      # 串口监视器
```

## ESP32-S3

### QEMU

```bash
source $HOME/esp/esp-idf/export.sh
make build-esp32s3-qemu
make run-esp32s3-qemu
make run-esp32s3-qemu GRAPHICS=1    # 带 LCD 显示窗口
make run-esp32s3-qemu GDB=1         # 调试模式（GDB 端口 1234）
```

### 真实硬件（16 MB Flash + 8 MB PSRAM）

```bash
source $HOME/esp/esp-idf/export.sh
make esp32s3                         # build-esp32s3-default 的别名
make flash-esp32s3
make monitor-esp32s3                 # 串口监视器
```

WiFi 配置：构建前使用 `idf.py menuconfig`，或在运行时使用 `/wifi_set`。

## QEMU vexpress-a9（RT-Thread）

无需 ESP-IDF，使用 Meson + SCons 构建。

```bash
export RTCLAW_AI_API_KEY='sk-...'
export RTCLAW_AI_API_URL='https://api.anthropic.com/v1/messages'
export RTCLAW_AI_MODEL='claude-sonnet-4-6'

make vexpress-a9-qemu

# RT-Thread 不支持 TLS — 启动 HTTP 转 HTTPS 代理
python3 scripts/api-proxy.py https://api.anthropic.com &

make run-vexpress-a9-qemu
make run-vexpress-a9-qemu GDB=1     # 调试模式（GDB 端口 1234）
```

> **注意：** vexpress-a9 的 RT-Thread 移植不支持 TLS。项目自带的
> `scripts/api-proxy.py` 负责将 HTTP（QEMU 侧）桥接到 HTTPS（上游 API）。
> 固件发起 AI 请求前必须先启动该代理。

## QEMU Zynq-A9（FreeRTOS + FreeRTOS+TCP）

无需 ESP-IDF，使用 Meson 完整固件构建。

```bash
export RTCLAW_AI_API_KEY='sk-...'
export RTCLAW_AI_API_URL='https://api.anthropic.com/v1/messages'
export RTCLAW_AI_MODEL='claude-sonnet-4-6'

make build-zynq-a9-qemu
make run-zynq-a9-qemu
make run-zynq-a9-qemu GDB=1        # 调试模式（GDB 端口 1234）
```

> **注意：** Zynq-A9 使用 FreeRTOS+TCP 配合 Cadence GEM 以太网。
> 与 vexpress-a9 一样，不支持 TLS — 需要使用 `scripts/api-proxy.py`
> 将 HTTP 桥接到 HTTPS 以发起 AI API 请求。

## Linux 原生平台

无需交叉编译器或 QEMU，直接在宿主机上构建和运行。

```bash
export RTCLAW_AI_API_KEY='sk-...'
export RTCLAW_AI_API_URL='https://api.anthropic.com/v1/messages'
export RTCLAW_AI_MODEL='claude-sonnet-4-6'

make build-linux
make run-linux
```

Linux OSAL 使用 pthreads、libcurl（含 TLS）和基于文件的 KV 存储。
在嵌入式 Linux 上部署时，请参阅[嵌入式 Linux HTTPS 注意事项](#嵌入式-linux-https-注意事项)。

### Linux Web 语音 MVP

如需启用 Linux 下的浏览器语音端点，请带上语音选项重新构建：

```bash
meson setup build/linux --reconfigure \
    -Dosal=linux \
    -Dvoice=true \
    -Dlinux_web_voice=true
meson compile -C build/linux
./build/linux/platform/linux/rtclaw
```

然后在 shell 中执行：

```text
/voice_enable on
/voice_set stt_provider xfyun
/voice_set stt_xfyun_app_id <APPID>
/voice_set stt_xfyun_api_key <APIKey>
/voice_set stt_xfyun_api_secret <APISecret>
/voice_status
```

最后在本机浏览器打开 `http://127.0.0.1:8080/voice.html` 测试 Linux web
语音页面。

如果 MiMo TTS 回复较长，语音服务会在可用时把解码后的音频分块流式转发给
端点。缓冲式回退路径仍然有两个可调项：`-Dvoice_tts_resp_size=` 用于包含
base64 音频的完整 HTTP JSON 响应，`-Dvoice_tts_audio_buf_size=` 用于 base64
解码后的音频输出。

## Zephyr（Cortex-A9 / Cortex-M3）

### 前置条件

- Zephyr SDK 1.0.1 —— 按照官方
  [Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/)
  安装
- 已初始化 Git 子模块（`git submodule update --init --recursive`）

无需 ESP-IDF。Zephyr 构建使用 CMake（west），并将 OSAL + claw 源码直接编译进
Zephyr 应用中（不经过 Meson 预编译 `.a` 文件）。

### Cortex-A9（需要 Xilinx QEMU）

```bash
export RTCLAW_AI_API_KEY='sk-...'
# TLS 默认尚未启用，通过 api-proxy 访问 HTTPS
export RTCLAW_AI_API_URL='http://10.0.2.2:8888/v1/messages'
export RTCLAW_AI_MODEL='claude-sonnet-4-6'

# 在另一个终端启动代理：python3 scripts/api-proxy.py
make build-zephyr-cortex-a9-qemu
make run-zephyr-cortex-a9-qemu
```

> **注意：** Cortex-A9 目标需要 Xilinx QEMU（`qemu-system-aarch64`
> 且具备 Xilinx 机型支持），不是标准上游 QEMU。

### Cortex-M3（标准 QEMU）

```bash
make build-zephyr-cortex-m3-qemu
make run-zephyr-cortex-m3-qemu
```

Cortex-M3 目标使用标准 `qemu-system-arm` 和系统 QEMU 包中自带的
`lm3s6965evb` 机型。

> **注意：** Zephyr 自带 mbedTLS 用于原生 HTTPS，但 TLS Kconfig 默认尚未
> 启用。在完成默认配置之前，请和 vexpress-a9、Zynq-A9 一样使用
> `scripts/api-proxy.py` 代理 AI API 的 HTTPS 请求。

## 嵌入式 Linux HTTPS 注意事项

在嵌入式 Linux 系统（如 Zynq PetaLinux、树莓派、Buildroot/Yocto 目标板）
上部署 Linux 原生构建时，连接 AI API 和 IM 服务的 HTTPS 需要完整的 TLS 基础设施。

### 系统时间

TLS 证书验证依赖准确的系统时间。嵌入式系统启动时通常是 epoch 时间
（1970-01-01），这会导致所有 HTTPS 连接因证书过期而失败。

**解决方法：** 启动时通过 NTP 同步时间。

```bash
# 一次性 NTP 同步（busybox / 嵌入式环境）
ntpd -q -p pool.ntp.org

# 或使用 chrony / systemd-timesyncd
timedatectl set-ntp true
```

如果没有网络 NTP，可以手动设置或从 RTC 读取：

```bash
date -s "2026-03-23 12:00:00"
hwclock -s                          # 从硬件 RTC 读取
```

> **症状：** libcurl 在启动后立即返回 `CURLE_SSL_CACERT` (60) 或
> `CURLE_PEER_FAILED_VERIFICATION` (51)。
> 检查 `date` — 如果显示 1970 年，说明时间同步是问题所在。

### CA 根证书

libcurl 需要 CA 根证书来验证 HTTPS 服务器身份。桌面 Linux 自带
`ca-certificates`，但精简的嵌入式根文件系统通常不包含。

**Debian / Ubuntu / PetaLinux：**

```bash
apt-get install -y ca-certificates
update-ca-certificates
```

**Buildroot：**

在 `make menuconfig` 中启用 `BR2_PACKAGE_CA_CERTIFICATES`。

**Yocto：**

在镜像配方中添加：

```
IMAGE_INSTALL:append = " ca-certificates"
```

**手动安装（任意系统）：**

```bash
# 下载 Mozilla CA 证书包
curl -o /etc/ssl/certs/ca-certificates.crt \
    https://curl.se/ca/cacert.pem

# 告诉 libcurl 证书位置
export CURL_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt
```

> **症状：** 时间正确但 libcurl 返回 `CURLE_SSL_CACERT` (60)。
> 检查 `/etc/ssl/certs/ca-certificates.crt` 是否存在。

### libcurl TLS 支持

Linux OSAL（`osal/linux/`）使用 libcurl 发起所有 HTTP/HTTPS 请求。
libcurl 必须构建时包含 TLS 后端支持。

**检查 TLS 支持：**

```bash
curl --version | grep -i ssl
# 应显示：OpenSSL/x.x.x 或 mbedTLS/x.x.x 等
```

**常见嵌入式 Linux 包名：**

| 发行版 | 包名 | TLS 后端 |
|--------|------|---------|
| Debian / Ubuntu | `libcurl4-openssl-dev` | OpenSSL |
| Buildroot | `BR2_PACKAGE_LIBCURL` + `BR2_PACKAGE_OPENSSL` | OpenSSL |
| Yocto | `libcurl`（配合 `PACKAGECONFIG:append = "ssl"`） | OpenSSL |
| Alpine | `curl-dev` | OpenSSL 或 mbedTLS |

> **症状：** Meson setup 报错 `Dependency "libcurl" not found`。
> 安装 `libcurl4-openssl-dev`（或对应包名）。

### DNS 解析

确保 `/etc/resolv.conf` 包含有效的 DNS 服务器：

```bash
echo "nameserver 8.8.8.8" > /etc/resolv.conf
```

没有 DNS 的话，libcurl 无法解析 `api.anthropic.com` 或 `open.feishu.cn`
等 API 域名。

### 快速自检清单

| 项目 | 检查方法 | 修复方法 |
|------|---------|---------|
| 系统时间 | `date` 显示当前年份 | NTP 同步或 `date -s` 手动设置 |
| CA 根证书 | `/etc/ssl/certs/ca-certificates.crt` 存在 | 安装 `ca-certificates` 包 |
| libcurl TLS | `curl --version` 显示 SSL | 安装带 OpenSSL 的 libcurl |
| DNS | `nslookup api.anthropic.com` 可解析 | 配置 `/etc/resolv.conf` |
| 网络连通 | `curl -I https://api.anthropic.com` 返回 200 | 检查防火墙/代理设置 |

## 使用 GDB 调试

所有 QEMU 目标都支持 `GDB=1` 参数，该参数会在启动时暂停 CPU 并在
端口 1234 上开启 GDB 服务器。

**ESP32-C3 示例：**

```bash
# 终端 1 — 以调试模式启动 QEMU
make run-esp32c3-qemu GDB=1

# 终端 2 — 连接 GDB
riscv32-esp-elf-gdb build/esp32c3-qemu/idf/rt-claw.elf \
    -ex 'target remote :1234'
```

**ESP32-S3 示例：**

```bash
# 终端 1
make run-esp32s3-qemu GDB=1

# 终端 2
xtensa-esp32s3-elf-gdb build/esp32s3-qemu/idf/rt-claw.elf \
    -ex 'target remote :1234'
```

**vexpress-a9 示例：**

```bash
# 终端 1
make run-vexpress-a9-qemu GDB=1

# 终端 2
arm-none-eabi-gdb build/vexpress-a9-qemu/rtthread.elf \
    -ex 'target remote :1234'
```

## CNB 云端 IDE

如果希望在浏览器中开发而无需本地环境配置，可在 CNB 上打开本项目：

<https://cnb.cool/gevico.online/rtclaw/rt-claw>

云端环境已预装 ESP-IDF 和 QEMU，开箱即用。
