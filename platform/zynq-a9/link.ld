/* SPDX-License-Identifier: MIT */
/*
 * Linker script for Zynq-7000 QEMU (FreeRTOS, Cortex-A9)
 * Based on Xilinx FreeRTOS Zynq demo lscript.ld, simplified for rt-claw.
 */

_STACK_SIZE = 0x4000;
_HEAP_SIZE = 0x4000;
_IRQ_STACK_SIZE = 0x1000;
_FIQ_STACK_SIZE = 0x400;
_SUPERVISOR_STACK_SIZE = 0x1000;
_ABORT_STACK_SIZE = 0x400;
_UNDEF_STACK_SIZE = 0x400;

ENTRY(_boot)

MEMORY
{
    /* Skip first 1MB (boot ROM area), DDR up to ~1GB */
    DDR : ORIGIN = 0x100000, LENGTH = 0x3FF00000
    /* On-chip memory, 192KB */
    OCM : ORIGIN = 0x0, LENGTH = 0x30000
}

SECTIONS
{
    /*
     * FreeRTOS vector table — must be first in DDR.
     * Contains ARM exception vectors (reset, undef, swi, prefetch abort,
     * data abort, IRQ, FIQ).
     */
    .boot : {
        *(.boot)
    } > DDR

    /* 32-byte aligned for ARM VBAR requirement */
    .freertos_vectors : ALIGN(32) {
        KEEP(*(.freertos_vectors))
        KEEP(*(.vectors))
    } > DDR

    /* Executable code */
    .text : ALIGN(4) {
        *(.text)
        *(.text.*)
        *(.gnu.linkonce.t.*)
    } > DDR

    /* Read-only data */
    .rodata : {
        __rodata_start = .;
        *(.rodata)
        *(.rodata.*)
        *(.gnu.linkonce.r.*)
        __rodata_end = .;
    } > DDR

    /* rt-claw OOP auto-registration sections */
    .claw_registry : {
        . = ALIGN(4);
        __start_claw_services = .;
        KEEP(*(SORT(claw_services*)))
        __stop_claw_services = .;

        . = ALIGN(4);
        __start_claw_drivers = .;
        KEEP(*(SORT(claw_drivers*)))
        __stop_claw_drivers = .;

        . = ALIGN(4);
        __start_claw_tools = .;
        KEEP(*(SORT(claw_tools*)))
        __stop_claw_tools = .;
    } > DDR

    /* ARM exception index (for unwinding) */
    .ARM.exidx : {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > DDR

    /* Initialized data */
    .data : {
        __data_start = .;
        *(.data)
        *(.data.*)
        *(.gnu.linkonce.d.*)
        __data_end = .;
    } > DDR

    /* Small data (for linker relaxation) */
    .sdata : {
        __sdata_start = .;
        *(.sdata)
        *(.sdata.*)
        __sdata_end = .;
    } > DDR

    /* Small BSS */
    .sbss (NOLOAD) : {
        __sbss_start = .;
        *(.sbss)
        *(.sbss.*)
        __sbss_end = .;
    } > DDR

    /* Zero-initialized data */
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        __bss_end = .;
    } > DDR

    _end = .;

    /*
     * MMU translation table — must be 16KB aligned.
     * Cortex-A9 L1 table: 4096 entries x 4 bytes = 16KB.
     */
    .mmu_tbl (NOLOAD) : ALIGN(16384) {
        __mmu_tbl_start = .;
        *(.mmu_tbl)
        __mmu_tbl_end = .;
    } > DDR

    /* Heap area */
    .heap (NOLOAD) : {
        . = ALIGN(16);
        _heap_start = .;
        . += _HEAP_SIZE;
        _heap_end = .;
    } > DDR

    /*
     * Stacks for all ARM processor modes.
     * Each stack grows downward; the _end symbol marks the top (high address).
     */
    .stack (NOLOAD) : {
        . = ALIGN(16);

        /* System / User mode stack */
        _stack_end = .;
        . += _STACK_SIZE;
        . = ALIGN(16);
        __stack = .;
        _stack = .;

        /* IRQ mode stack */
        _irq_stack_end = .;
        . += _IRQ_STACK_SIZE;
        . = ALIGN(16);
        __irq_stack = .;

        /* Supervisor mode stack */
        _supervisor_stack_end = .;
        . += _SUPERVISOR_STACK_SIZE;
        . = ALIGN(16);
        __supervisor_stack = .;

        /* Abort mode stack */
        _abort_stack_end = .;
        . += _ABORT_STACK_SIZE;
        . = ALIGN(16);
        __abort_stack = .;

        /* FIQ mode stack */
        _fiq_stack_end = .;
        . += _FIQ_STACK_SIZE;
        . = ALIGN(16);
        __fiq_stack = .;

        /* Undefined instruction mode stack */
        _undef_stack_end = .;
        . += _UNDEF_STACK_SIZE;
        . = ALIGN(16);
        __undef_stack = .;
    } > DDR
}
