/* SPDX-License-Identifier: MIT */
/*
 * Minimal newlib syscall stubs for bare-metal Zynq QEMU.
 * _write uses UART0 directly for printf output.
 */

#include <sys/stat.h>
#include <errno.h>

/*
 * Zynq PS UART (Cadence UART) registers.
 * QEMU maps UART0 at 0xE0000000, UART1 at 0xE0001000.
 * -nographic connects the first UART to stdio.
 */
#define UART_BASE  0xE0000000U

/* Cadence UART register offsets */
#define UART_CR    (*(volatile unsigned int *)(UART_BASE + 0x00))
#define UART_MR    (*(volatile unsigned int *)(UART_BASE + 0x04))
#define UART_SR    (*(volatile unsigned int *)(UART_BASE + 0x2C))
#define UART_FIFO  (*(volatile unsigned int *)(UART_BASE + 0x30))

#define UART_CR_TX_EN   (1U << 4)
#define UART_CR_RX_EN   (1U << 2)
#define UART_SR_TXFULL  (1U << 4)

static int uart_inited = 0;

static void uart_init(void)
{
    if (!uart_inited) {
        /* Enable TX and RX */
        UART_CR = UART_CR_TX_EN | UART_CR_RX_EN;
        uart_inited = 1;
    }
}

int _write(int fd, const char *buf, int len)
{
    (void)fd;
    uart_init();
    for (int i = 0; i < len; i++) {
        while (UART_SR & UART_SR_TXFULL) {
            /* wait */
        }
        UART_FIFO = (unsigned int)buf[i];
    }
    return len;
}

int _read(int fd, char *buf, int len)
{
    (void)fd; (void)buf; (void)len;
    return 0;
}

int _close(int fd)
{
    (void)fd;
    return -1;
}

int _lseek(int fd, int offset, int whence)
{
    (void)fd; (void)offset; (void)whence;
    return 0;
}

int _fstat(int fd, struct stat *st)
{
    (void)fd;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int fd)
{
    (void)fd;
    return 1;
}

void *_sbrk(int incr)
{
    extern char _heap_start;
    extern char _heap_end;
    static char *heap_ptr = 0;

    if (heap_ptr == 0) {
        heap_ptr = &_heap_start;
    }

    char *prev = heap_ptr;
    if (heap_ptr + incr > &_heap_end) {
        errno = ENOMEM;
        return (void *)-1;
    }
    heap_ptr += incr;
    return prev;
}

void _exit(int status)
{
    (void)status;
    while (1) {
        /* halt */
    }
}

int _kill(int pid, int sig)
{
    (void)pid; (void)sig;
    errno = EINVAL;
    return -1;
}

int _getpid(void)
{
    return 1;
}

void _init(void) {}
void _fini(void) {}

/* Xilinx BSP stubs for single-core QEMU */
#include "xil_types.h"

int Xil_IsSpinLockEnabled(void) { return 0; }
int XEmacPs_IsHighSpeedPCS(u32 base) { (void)base; return 0; }
void XEmacPs_SetupPCS(u32 base) { (void)base; }
void Xil_SpinLock(void) {}
void Xil_SpinUnlock(void) {}
void XTime_SetTime(u64 time) { (void)time; }

/*
 * MMU table placeholder — xil_mmu.c references MMUTable from
 * translation_table.S, but we skip MMU init on QEMU.
 * Provide a dummy 16KB-aligned array to satisfy the linker.
 */
u32 MMUTable[4096] __attribute__((aligned(16384)));
u32 XGetCoreId(void) { return 0; }

/* FreeRTOS+TCP application hooks */
#include <stdlib.h>
#include "FreeRTOS.h"

BaseType_t xApplicationGetRandomNumber(uint32_t *pulNumber)
{
    /* Simple PRNG — sufficient for DHCP/DNS on QEMU */
    *pulNumber = (uint32_t)rand();
    return pdTRUE;
}

uint32_t ulApplicationGetNextSequenceNumber(uint32_t a, uint16_t b,
                                            uint32_t c, uint16_t d)
{
    (void)a; (void)b; (void)c; (void)d;
    return (uint32_t)rand();
}

const char *pcApplicationHostnameHook(void)
{
    return "rt-claw";
}

/* ARM exception handler stubs */
void FIQInterrupt(void) { while (1); }

void DataAbortInterrupt(void)
{
    extern int printf(const char *, ...);
    unsigned int dfar, dfsr, lr;
    __asm__ volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(dfar));
    __asm__ volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(dfsr));
    __asm__ volatile("mov %0, lr" : "=r"(lr));
    printf("DATA ABORT: DFAR=0x%08x DFSR=0x%08x LR=0x%08x\n",
           dfar, dfsr, lr);
    printf("  Fault at PC=0x%08x\n", lr - 8);
    while (1) {
    }
}

void PrefetchAbortInterrupt(void) { while (1); }
