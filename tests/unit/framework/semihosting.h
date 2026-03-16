/*
 * SPDX-License-Identifier: MIT
 * ARM semihosting exit for QEMU.
 */

#ifndef RTCLAW_SEMIHOSTING_H
#define RTCLAW_SEMIHOSTING_H

static inline void semihosting_exit(int code)
{
    unsigned long args[2] = { 0x20026, (unsigned long)code };
    register unsigned long r0 __asm__("r0") = 0x20;
    register unsigned long r1 __asm__("r1") = (unsigned long)args;
    __asm__ volatile("svc #0x123456" : : "r"(r0), "r"(r1) : "memory");
    __builtin_unreachable();
}

#endif /* RTCLAW_SEMIHOSTING_H */
