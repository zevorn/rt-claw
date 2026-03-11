# rt-claw

Real-Time Claw — an OpenClaw-inspired intelligent assistant for embedded devices.

Multi-RTOS support via OSAL. Build swarm intelligence with networked nodes.

## Architecture

```
+---------------------------------------------------+
|                rt-claw Application                |
|   gateway  |  swarm  |  net_service  |  ai_engine |
+---------------------------------------------------+
|               claw_os.h  (OSAL API)               |
+-----------------+---------------------------------+
| FreeRTOS (IDF)  |          RT-Thread              |
+-----------------+---------------------------------+
| ESP32-C3        |  QEMU vexpress-a9               |
| WiFi / BLE      |  Ethernet / UART                |
+-----------------+---------------------------------+
```

## Supported Platforms

| Platform | RTOS | Build System | Status |
|----------|------|-------------|--------|
| ESP32-C3 | ESP-IDF + FreeRTOS | CMake (idf.py) | WIP |
| QEMU vexpress-a9 | RT-Thread | SCons | Working |

## Quick Start

### QEMU vexpress-a9 (RT-Thread)

```bash
# Prerequisites: arm-none-eabi-gcc, qemu-system-arm, scons
cd platform/qemu-a9-rtthread
scons -j$(nproc)
../../tools/qemu-run.sh
```

### ESP32-C3 (ESP-IDF)

```bash
# Prerequisites: ESP-IDF v5.x, Espressif QEMU
cd platform/esp32c3
idf.py set-target esp32c3
idf.py build
idf.py qemu monitor         # QEMU
idf.py -p /dev/ttyUSB0 flash monitor  # real hardware
```

## Project Structure

```
rt-claw/
├── osal/                    # OS Abstraction Layer
│   ├── include/claw_os.h   #   Unified RTOS API
│   ├── freertos/            #   FreeRTOS implementation
│   └── rtthread/            #   RT-Thread implementation
├── src/                     # Platform-independent core
│   ├── core/gateway.*       #   Message routing
│   ├── services/swarm/      #   Swarm intelligence
│   ├── services/net/        #   Network service
│   └── services/ai/         #   AI inference engine
├── platform/
│   ├── esp32c3/             # ESP-IDF project (CMake)
│   └── qemu-a9-rtthread/   # RT-Thread BSP (SCons)
├── vendor/
│   ├── freertos/            # FreeRTOS-Kernel (submodule)
│   └── rt-thread/           # RT-Thread (submodule)
└── tools/                   # Build & launch scripts
```

## License

MIT
