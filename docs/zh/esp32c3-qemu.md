# ESP32-C3 QEMU 指南

[English](../en/esp32c3-qemu.md) | **中文**

## 概述

Espressif 维护了一个 QEMU 分支（[espressif/qemu](https://github.com/espressif/qemu)），
支持 ESP32-C3 仿真，具备以下外设支持：

| 外设 | 状态 |
|------|------|
| CPU（RISC-V rv32imc） | 支持 |
| SPI Flash（2/4/8/16MB） | 支持 |
| UART0 | 支持 |
| GPIO | 支持 |
| Timer Group（TIMG） | 支持（看门狗可禁用） |
| SHA、AES、RSA、HMAC | 支持 |
| Flash 加密 | 支持 |
| OpenCores 以太网 MAC | 支持（虚拟网络） |
| eFuse | 支持 |
| WiFi / BLE | **不支持** |

**重要**：WiFi 和 BLE 不支持仿真。QEMU 中的网络测试使用
虚拟 OpenCores 以太网 MAC，参数为 `-nic user,model=open_eth`。

## 前置依赖

### Arch Linux

```bash
sudo pacman -S --needed \
    libgcrypt glib2 pixman sdl2 libslirp \
    python cmake ninja gcc git wget flex bison
```

### ESP-IDF + QEMU 安装

```bash
# 一次性安装（克隆 ESP-IDF + 安装工具链 + QEMU）
./tools/setup-esp-env.sh

# 激活环境（每个终端会话）
source $HOME/esp/esp-idf/export.sh
```

安装内容：
- `riscv32-esp-elf-gcc` — ESP32-C3 RISC-V 工具链
- `qemu-system-riscv32` — Espressif QEMU（ESP32-C3 机器支持）
- `esptool.py` — Flash 镜像工具

## 构建

```bash
source $HOME/esp/esp-idf/export.sh

# 推荐：统一构建
make esp32c3

# 或分步操作：
cd platform/esp32c3
idf.py set-target esp32c3          # 仅首次
idf.py reconfigure                 # 生成 compile_commands.json
cd ../..
python3 scripts/gen-esp32c3-cross.py  # 生成 Meson 交叉编译文件
meson setup build/esp32c3 --cross-file platform/esp32c3/cross.ini
meson compile -C build/esp32c3     # 交叉编译核心代码
cd platform/esp32c3
idf.py build                       # 链接生成最终固件
```

## 在 QEMU 上运行

### 方式一：统一脚本（推荐）

```bash
./tools/qemu-run.sh -m esp32c3
```

### 方式二：idf.py 封装

```bash
cd platform/esp32c3
idf.py qemu monitor
```

按 `Ctrl+]` 退出。

此命令会生成合并的 flash 镜像并执行：
```
qemu-system-riscv32 -nographic -icount 3 \
    -machine esp32c3 \
    -drive file=build/flash_image.bin,if=mtd,format=raw \
    -global driver=timer.esp32c3.timg,property=wdt_disable,value=true \
    -nic user,model=open_eth
```

### 关键 QEMU 参数

| 参数 | 用途 |
|------|------|
| `-machine esp32c3` | ESP32-C3 机器模型 |
| `-icount 3` | 指令定时（8ns/指令，约 125MHz） |
| `-drive file=X,if=mtd,format=raw` | Flash 镜像 |
| `-nographic` | UART 输出到 stdio，无显示窗口 |
| `-nic user,model=open_eth` | 虚拟以太网（用户模式 NAT） |
| `-global driver=timer.esp32c3.timg,property=wdt_disable,value=true` | 禁用看门狗 |
| `-s -S` | GDB 服务器监听 1234 端口，启动时暂停 |

## 调试

### 方式一：idf.py

```bash
# 终端 1
cd platform/esp32c3
idf.py qemu --gdb monitor

# 终端 2
cd platform/esp32c3
idf.py gdb
```

### 方式二：直接 GDB

```bash
# 终端 1
./tools/qemu-run.sh -m esp32c3 -g

# 终端 2
cd platform/esp32c3
riscv32-esp-elf-gdb build/rt-claw.elf -ex 'target remote :1234'
```

## QEMU 中的网络

WiFi 不支持仿真。QEMU 通过 OpenCores MAC 提供虚拟以太网。

启用方式：`-nic user,model=open_eth`

端口转发（例如暴露 MQTT 端口）：
```
-nic user,model=open_eth,hostfwd=tcp:127.0.0.1:1883-:1883
```

在 rt-claw ESP32-C3 代码中，通常使用 WiFi 的网络服务需要
编译时选项来在 QEMU 上切换为以太网。

## Flash 镜像生成

ESP-IDF 的 `idf.py qemu` 会自动处理。手动生成：

```bash
cd platform/esp32c3
esptool.py --chip esp32c3 merge_bin \
    --fill-flash-size 4MB \
    -o build/flash_image.bin \
    @build/flash_args
```

`flash_args` 文件由 `idf.py build` 生成，内容如下：
```
0x0     build/bootloader/bootloader.bin
0x8000  build/partition_table/partition-table.bin
0x10000 build/rt-claw.bin
```

## 已知限制

- WiFi 和蓝牙不支持仿真
- 必须使用 `-icount`（不支持自由运行模式）
- RTC 看门狗定时器未实现
- Secure Boot 在 QEMU 中不支持
- esptool 无法通过 TCP 的 RTS 复位芯片

## 参考资料

- [ESP-IDF QEMU 指南](https://docs.espressif.com/projects/esp-idf/zh_CN/stable/esp32c3/api-guides/tools/qemu.html)
- [Espressif QEMU 分支](https://github.com/espressif/qemu)
- [ESP32-C3 QEMU README](https://github.com/espressif/esp-toolchain-docs/blob/main/qemu/esp32c3/README.md)
