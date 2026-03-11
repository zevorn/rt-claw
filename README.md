# rt-claw

Real-Time Claw — an OpenClaw-inspired intelligent assistant running on RT-Thread RTOS.

Run claw on any embedded device. Build swarm intelligence with networked nodes.

## Architecture

```
┌─────────────────────────────────────────┐
│            Applications (claw_app)       │
├─────────────────────────────────────────┤
│  ┌──────────┐ ┌────────┐ ┌───────────┐ │
│  │ gateway   │ │ swarm  │ │ ai_engine │ │
│  │ (message) │ │ (mesh) │ │(inference)│ │
│  └──────────┘ └────────┘ └───────────┘ │
├─────────────────────────────────────────┤
│  RT-Thread Kernel + Components          │
│  (Thread, IPC, FinSH, Device, lwIP)     │
├─────────────────────────────────────────┤
│  BSP / HAL (qemu-vexpress-a9)           │
└─────────────────────────────────────────┘
```

## Prerequisites

- `arm-none-eabi-gcc` toolchain
- `qemu-system-arm`
- `scons`
- `python3`

## Build

```bash
scons -j$(nproc)
```

## Run

```bash
./tools/qemu-run.sh
```

## Debug

```bash
# Terminal 1: start QEMU with GDB server
./tools/qemu-dbg.sh

# Terminal 2: connect GDB
arm-none-eabi-gdb -ex 'target remote :1234' rtthread.elf
```

## Project Structure

```
rt-claw/
├── applications/        # Application entry (main.c)
├── drivers/             # BSP drivers (qemu-vexpress-a9)
├── src/
│   ├── core/            # Message gateway
│   └── services/
│       ├── swarm/       # Swarm node discovery & coordination
│       ├── net/         # Network service (lwIP)
│       └── ai/          # Lightweight inference engine
├── vendor/rt-thread/    # RT-Thread source (git submodule)
└── tools/               # QEMU launch scripts
```

## License

MIT
