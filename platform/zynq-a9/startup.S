/* SPDX-License-Identifier: MIT */
/*
 * Minimal startup for Zynq-7000 QEMU + FreeRTOS.
 * Skips cache/MMU/L2 init (not needed on QEMU).
 * Sets up ARM mode stacks and jumps to main().
 */

.text
.arm

.global _boot
.global _vector_table

.extern _freertos_vector_table
.extern __irq_stack
.extern __supervisor_stack
.extern __abort_stack
.extern __fiq_stack
.extern __undef_stack
.extern __stack
.extern __bss_start
.extern __bss_end
.extern main

.section .boot, "ax"

_boot:
_vector_table:
    b   _reset
    b   .           /* Undefined */
    b   .           /* SWI */
    b   .           /* Prefetch abort */
    b   .           /* Data abort */
    nop             /* Reserved */
    b   .           /* IRQ */
    b   .           /* FIQ */

_reset:
    /* Disable interrupts */
    cpsid   if

    /* Enable VFP/NEON (CP10 + CP11 full access) */
    mrc p15, 0, r0, c1, c0, 2
    orr r0, r0, #(0xF << 20)
    mcr p15, 0, r0, c1, c0, 2
    isb
    mov r0, #(1 << 30)          /* FPEXC.EN */
    vmsr fpexc, r0

    /* Allow unaligned memory access (clear SCTLR.A bit).
     * FreeRTOS+TCP uses 2-byte packet filler for IP header alignment,
     * which causes unaligned 32-bit accesses. */
    mrc p15, 0, r0, c1, c0, 0
    bic r0, r0, #(1 << 1)       /* Clear A (alignment check) bit */
    mcr p15, 0, r0, c1, c0, 0
    isb

    /* Set VBAR to FreeRTOS vector table */
    ldr r0, =_freertos_vector_table
    mcr p15, 0, r0, c12, c0, 0
    isb

    /* Setup IRQ stack */
    cps #0x12
    ldr sp, =__irq_stack

    /* Setup Supervisor stack */
    cps #0x13
    ldr sp, =__supervisor_stack

    /* Setup Abort stack */
    cps #0x17
    ldr sp, =__abort_stack

    /* Setup FIQ stack */
    cps #0x11
    ldr sp, =__fiq_stack

    /* Setup Undefined stack */
    cps #0x1b
    ldr sp, =__undef_stack

    /* Switch to Supervisor mode — FreeRTOS ARM_CA9 port
     * expects to start the scheduler from SVC mode. */
    cps #0x13

    /* Clear BSS */
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
1:
    cmp r0, r1
    strlt r2, [r0], #4
    blt 1b

    /* Jump to main */
    bl  main

    /* Should not return */
    b   .
