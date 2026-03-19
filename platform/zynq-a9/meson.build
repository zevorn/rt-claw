# SPDX-License-Identifier: MIT
# Zynq-A9 QEMU platform: FreeRTOS kernel + Xilinx BSP + FreeRTOS+TCP + rt-claw → ELF

bsp_root = project_root / 'vendor' / 'bsp' / 'xilinx'
freertos_root = project_root / 'vendor' / 'os' / 'freertos'
tcpip_root = project_root / 'vendor' / 'lib' / 'freertos-plus-tcp'
platform_dir = project_root / 'platform' / 'zynq-a9'

# ---- Xilinx BSP ----

bsp_incs = include_directories(
    '../../vendor/bsp/xilinx',
    '../../vendor/bsp/xilinx/standalone/common',
    '../../vendor/bsp/xilinx/standalone/cortexa9',
    '../../vendor/bsp/xilinx/drivers/scugic',
    '../../vendor/bsp/xilinx/drivers/scutimer',
    '../../vendor/bsp/xilinx/drivers/emacps',
    '../../vendor/bsp/xilinx/drivers/uartps',
)

bsp_src = files(
    '../../vendor/bsp/xilinx/standalone/common/xil_assert.c',
    '../../vendor/bsp/xilinx/standalone/common/xil_printf.c',
    '../../vendor/bsp/xilinx/standalone/common/xil_exception.c',
    '../../vendor/bsp/xilinx/standalone/common/xplatform_info.c',
    '../../vendor/bsp/xilinx/standalone/cortexa9/xil_cache.c',
    '../../vendor/bsp/xilinx/standalone/cortexa9/xil_mmu.c',
    '../../vendor/bsp/xilinx/drivers/scugic/xscugic.c',
    '../../vendor/bsp/xilinx/drivers/scugic/xscugic_hw.c',
    '../../vendor/bsp/xilinx/drivers/scugic/xscugic_g.c',
    '../../vendor/bsp/xilinx/drivers/scugic/xscugic_sinit.c',
    '../../vendor/bsp/xilinx/drivers/scugic/xscugic_intr.c',
    '../../vendor/bsp/xilinx/drivers/scutimer/xscutimer.c',
    '../../vendor/bsp/xilinx/drivers/scutimer/xscutimer_g.c',
    '../../vendor/bsp/xilinx/drivers/scutimer/xscutimer_sinit.c',
    '../../vendor/bsp/xilinx/drivers/emacps/xemacps.c',
    '../../vendor/bsp/xilinx/drivers/emacps/xemacps_bdring.c',
    '../../vendor/bsp/xilinx/drivers/emacps/xemacps_control.c',
    '../../vendor/bsp/xilinx/drivers/emacps/xemacps_g.c',
    '../../vendor/bsp/xilinx/drivers/emacps/xemacps_hw.c',
    '../../vendor/bsp/xilinx/drivers/emacps/xemacps_intr.c',
    '../../vendor/bsp/xilinx/drivers/emacps/xemacps_sinit.c',
)

bsp_lib = static_library('xilinx_bsp',
    bsp_src,
    include_directories: bsp_incs,
    c_args: ['-DARMR5', '-w'],
)

# ---- FreeRTOS Kernel (ARM_CA9 port) ----

freertos_incs = include_directories(
    '../../vendor/os/freertos/include',
    '../../vendor/os/freertos/portable/GCC/ARM_CA9',
    '.',
)

freertos_src = files(
    '../../vendor/os/freertos/tasks.c',
    '../../vendor/os/freertos/queue.c',
    '../../vendor/os/freertos/list.c',
    '../../vendor/os/freertos/timers.c',
    '../../vendor/os/freertos/event_groups.c',
    '../../vendor/os/freertos/stream_buffer.c',
    '../../vendor/os/freertos/portable/GCC/ARM_CA9/port.c',
    '../../vendor/os/freertos/portable/GCC/ARM_CA9/portASM.S',
    '../../vendor/os/freertos/portable/MemMang/heap_4.c',
)

freertos_lib = static_library('freertos',
    freertos_src,
    include_directories: [freertos_incs, bsp_incs],
    c_args: ['-w'],
)

# ---- FreeRTOS+TCP ----

tcpip_incs = include_directories(
    '../../vendor/lib/freertos-plus-tcp/source/include',
    '../../vendor/lib/freertos-plus-tcp/source/portable/Compiler/GCC',
)

tcpip_src = files(
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_ARP.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_BitConfig.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_DHCP.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_DNS.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_DNS_Cache.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_DNS_Callback.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_DNS_Networking.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_DNS_Parser.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_ICMP.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_IP.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_IP_Timers.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_IP_Utils.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_IPv4.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_IPv4_Sockets.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_IPv4_Utils.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_Routing.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_Sockets.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_Stream_Buffer.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_TCP_IP.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_TCP_IP_IPv4.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_TCP_Reception.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_TCP_State_Handling.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_TCP_State_Handling_IPv4.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_TCP_Transmission.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_TCP_Transmission_IPv4.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_TCP_Utils.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_TCP_Utils_IPv4.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_TCP_WIN.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_Tiny_TCP.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_UDP_IP.c',
    '../../vendor/lib/freertos-plus-tcp/source/FreeRTOS_UDP_IPv4.c',
    '../../vendor/lib/freertos-plus-tcp/source/portable/BufferManagement/BufferAllocation_2.c',
    'drivers/NetworkInterface.c',
    '../../vendor/lib/freertos-plus-tcp/source/portable/NetworkInterface/Zynq/uncached_memory.c',
    '../../vendor/lib/freertos-plus-tcp/source/portable/NetworkInterface/Zynq/x_emacpsif_dma.c',
    '../../vendor/lib/freertos-plus-tcp/source/portable/NetworkInterface/Zynq/x_emacpsif_hw.c',
    '../../vendor/lib/freertos-plus-tcp/source/portable/NetworkInterface/Zynq/x_emacpsif_physpeed.c',
)

tcpip_zynq_incs = include_directories(
    '../../vendor/lib/freertos-plus-tcp/source/portable/NetworkInterface/include',
    '../../vendor/lib/freertos-plus-tcp/source/portable/NetworkInterface',
    '../../vendor/lib/freertos-plus-tcp/source/portable/NetworkInterface/Zynq',
)

tcpip_lib = static_library('freertos_tcp',
    tcpip_src,
    include_directories: [
        tcpip_incs,
        tcpip_zynq_incs,
        freertos_incs,
        bsp_incs,
    ],
    c_args: ['-w', '-mno-unaligned-access'],
)

# ---- Platform sources ----

platform_src = files(
    'startup.S',
    'FreeRTOS_asm_vectors.S',
    'main.c',
    'shell.c',
    'drivers/console.c',
    'syscalls.c',
    'boards/qemu/board.c',
)

all_incs = [freertos_incs, tcpip_incs, tcpip_zynq_incs, bsp_incs,
            claw_inc, claw_root_inc]

# ---- Link everything into rtclaw.elf ----

rtclaw_elf = executable('rtclaw',
    platform_src,
    include_directories: all_incs,
    c_args: platform_defs + ['-DCLAW_PLATFORM_FREERTOS'],
    link_whole: [rtclaw_lib, osal_lib],
    link_with: [cjson_lib, tcpip_lib, bsp_lib, freertos_lib],
    link_args: [
        '-T', platform_dir / 'link.ld',
        '-Wl,--gc-sections',
        '-Wl,--allow-multiple-definition',
        '-nostartfiles',
        '-Xlinker', '--defsym=_HEAP_SIZE=0x80000',
        '-Xlinker', '--defsym=_STACK_SIZE=0x4000',
    ],
    name_suffix: 'elf',
)

# ---- Unit test ELF ----

test_src = files(
    'startup.S',
    'FreeRTOS_asm_vectors.S',
    'test_main.c',
    'syscalls.c',
)

test_incs = include_directories(
    '../..',
)

rtclaw_test_elf = executable('rtclaw_test',
    test_src,
    include_directories: all_incs + [test_incs],
    c_args: platform_defs + ['-DCLAW_PLATFORM_FREERTOS'],
    link_with: [unittest_lib, rtclaw_lib, osal_lib, cjson_lib,
                tcpip_lib, bsp_lib, freertos_lib],
    link_args: [
        '-T', platform_dir / 'link.ld',
        '-Wl,--gc-sections',
        '-Wl,--allow-multiple-definition',
        '-nostartfiles',
        '-Xlinker', '--defsym=_HEAP_SIZE=0x80000',
        '-Xlinker', '--defsym=_STACK_SIZE=0x4000',
    ],
    name_suffix: 'elf',
)
