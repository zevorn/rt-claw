# rt-claw Architecture

## Overview

rt-claw brings the OpenClaw personal assistant concept to embedded RTOS devices.
Each rt-claw node is a lightweight, real-time capable unit that can operate
standalone or join a swarm of nodes for distributed intelligence.

## Core Concepts

### Gateway (src/core/gateway)

Central message router inspired by OpenClaw's Gateway architecture.
All inter-service and external communication flows through the gateway.

- Thread-safe message queue (RT-Thread `rt_messagequeue`)
- Channel-based routing (source → destination)
- Message types: DATA, CMD, EVENT, SWARM

### Swarm Service (src/services/swarm)

Node discovery and coordination for building a mesh of rt-claw devices.

- UDP broadcast discovery on local network
- Heartbeat-based liveness detection
- Capability advertisement (what each node can do)
- Task distribution across the swarm

### Network Service (src/services/net)

lwIP-based networking layer providing:

- TCP/IP connectivity
- MQTT client for cloud/broker communication
- HTTP client for API calls
- mDNS for local service discovery

### AI Engine (src/services/ai)

Lightweight inference runtime for embedded decision making.

- TinyML model loading and execution
- Decision pipeline with configurable rules
- Swarm-coordinated inference (split workloads across nodes)

## Communication Flow

```
External (MQTT/HTTP)
       │
       ▼
  ┌─────────┐
  │ net_svc  │
  └────┬─────┘
       │
       ▼
  ┌─────────┐     ┌─────────┐
  │ gateway  │────▶│ swarm   │──── other rt-claw nodes
  └────┬─────┘     └─────────┘
       │
       ▼
  ┌─────────┐
  │ai_engine│
  └─────────┘
```

## BSP: QEMU vexpress-a9

Development target: ARM Cortex-A9 on QEMU.
This provides a zero-hardware development environment with:

- Dual-core ARM Cortex-A9
- UART console (FinSH shell)
- SMC911x Ethernet (for network services)
- PL111 LCD controller (future GUI)
- SD card (FAT filesystem)
