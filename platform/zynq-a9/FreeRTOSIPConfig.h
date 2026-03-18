/* SPDX-License-Identifier: MIT */
/*
 * FreeRTOS+TCP configuration for Zynq-A9 QEMU.
 */

#ifndef FREERTOS_IP_CONFIG_H
#define FREERTOS_IP_CONFIG_H

/* Use DHCP to get IP address from QEMU SLIRP. */
#define ipconfigUSE_DHCP                        1
#define ipconfigDHCP_REGISTER_HOSTNAME          1

/* DNS */
#define ipconfigUSE_DNS                         1
#define ipconfigDNS_USE_CALLBACKS               0

/* Network buffers */
#define ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS  60
#define ipconfigNETWORK_MTU                     1500
#define ipconfigUSE_LINKED_RX_MESSAGES          1

/* TCP */
#define ipconfigUSE_TCP                         1
#define ipconfigTCP_MSS                         1460
#define ipconfigTCP_RX_BUFFER_LENGTH            (4 * ipconfigTCP_MSS)
#define ipconfigTCP_TX_BUFFER_LENGTH            (4 * ipconfigTCP_MSS)
#define ipconfigTCP_HANG_PROTECTION             1
#define ipconfigTCP_KEEP_ALIVE                  1

/* Sockets */
#define ipconfigALLOW_SOCKET_SEND_WITHOUT_BIND  1

/* ARP */
#define ipconfigARP_CACHE_ENTRIES               6
#define ipconfigMAX_ARP_RETRANSMISSIONS         5
#define ipconfigMAX_ARP_AGE                     150
#define ipconfigUSE_ARP_REVERSED_LOOKUP         0
#define ipconfigUSE_ARP_REMOVE_ENTRY            1

/* IP */
#define ipconfigIP_TASK_PRIORITY                (configMAX_PRIORITIES - 2)
#define ipconfigIP_TASK_STACK_SIZE_WORDS         (configMINIMAL_STACK_SIZE * 5)
#define ipconfigCOMPATIBLE_WITH_SINGLE           0
#define ipconfigIPv4_BACKWARD_COMPATIBLE         1

/* Buffer allocation scheme 2 (heap) */
#define ipconfigBUFFER_PADDING                  10
#define ipconfigPACKET_FILLER_SIZE              2

/* Byte order: ARM is little-endian */
#define ipconfigBYTE_ORDER                      pdFREERTOS_LITTLE_ENDIAN

/* Disable IPv6 — only IPv4 needed for QEMU SLIRP */
#define ipconfigUSE_IPv6                        0
#define ipconfigUSE_RA                          0

/* Callbacks */
#define ipconfigUSE_NETWORK_EVENT_HOOK          1

/* Logging — disabled to keep shell output clean */
#define ipconfigHAS_PRINTF                      0
#define ipconfigHAS_DEBUG_PRINTF                0

/* Multi-interface support (required by latest FreeRTOS+TCP) */
#define ipconfigUSE_DHCP_HOOK                   0
#define ipconfigSUPPORT_SELECT_FUNCTION         0
#define ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES   1

/* Zynq DMA zero-copy required by the Zynq NetworkInterface driver */
#define ipconfigZERO_COPY_RX_DRIVER             1
#define ipconfigZERO_COPY_TX_DRIVER             1
#define ipconfigNIC_N_TX_DESC                   32
#define ipconfigNIC_N_RX_DESC                   32

/*
 * QEMU Cadence GEM does not emulate PHY autonegotiation.
 * Force 100 Mbps link speed instead of auto-detect.
 */
#define ipconfigNIC_LINKSPEED100                1

/* configEMAC_TASK_STACK_SIZE for the GEM handler task */
#define configEMAC_TASK_STACK_SIZE              (configMINIMAL_STACK_SIZE * 4)

/* Reduce uncached memory from 1MB to 64KB for QEMU (no real cache) */
#define uncMEMORY_SIZE                          0x10000U

/* Socket semaphore count */
#define ipconfigSOCK_DEFAULT_RECEIVE_BLOCK_TIME pdMS_TO_TICKS(5000)
#define ipconfigSOCK_DEFAULT_SEND_BLOCK_TIME    pdMS_TO_TICKS(5000)

/* DHCP timing: increase timeouts for slow QEMU initialization */
#define ipconfigDHCP_FALL_BACK_AUTO_IP          0
#define ipconfigMAXIMUM_DISCOVER_TX_PERIOD      pdMS_TO_TICKS(30000)

#endif /* FREERTOS_IP_CONFIG_H */
