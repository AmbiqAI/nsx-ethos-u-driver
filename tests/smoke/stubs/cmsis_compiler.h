/*
 * SPDX-FileCopyrightText: Copyright 2026 Ambiq Micro, Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * SMOKE-BUILD STUB -- NOT FOR PRODUCTION USE.
 *
 * Minimal stand-in for CMSIS-Core's <cmsis_compiler.h>, sufficient for the
 * CI build-smoke harness (tests/smoke). The vendored Arm driver includes
 * this header for `__WFE()` / `__SEV()`; the NSX overrides in src/ also use
 * the PRIMASK helpers. Real builds get the genuine article via
 * `nsx::cmsis_core`.
 *
 * Everything here is either a real inline-asm intrinsic (when compiling for
 * Arm) or a benign host no-op, so the same harness works with host gcc/clang
 * and with arm-none-eabi-gcc.
 */

#ifndef NSX_SMOKE_CMSIS_COMPILER_H
#define NSX_SMOKE_CMSIS_COMPILER_H

#include <stdint.h>

#ifndef __STATIC_FORCEINLINE
#define __STATIC_FORCEINLINE static inline __attribute__((always_inline))
#endif
#ifndef __STATIC_INLINE
#define __STATIC_INLINE static inline
#endif
#ifndef __INLINE
#define __INLINE inline
#endif
#ifndef __ASM
#define __ASM __asm
#endif
#ifndef __USED
#define __USED __attribute__((used))
#endif
#ifndef __WEAK
#define __WEAK __attribute__((weak))
#endif
#ifndef __PACKED
#define __PACKED __attribute__((packed))
#endif
#ifndef __NO_RETURN
#define __NO_RETURN __attribute__((__noreturn__))
#endif

/*
 * Guard on the M-profile marker, not on __ARM_ARCH: an Apple-silicon or
 * aarch64 CI host defines __ARM_ARCH too, but has no PRIMASK and no WFE/SEV
 * event register in the Cortex-M sense.
 */
#if defined(__ARM_ARCH_PROFILE) && (__ARM_ARCH_PROFILE == 'M')

#define __NOP()  __asm volatile("nop")
#define __WFE()  __asm volatile("wfe")
#define __WFI()  __asm volatile("wfi")
#define __SEV()  __asm volatile("sev")
#define __DSB()  __asm volatile("dsb 0xF" ::: "memory")
#define __DMB()  __asm volatile("dmb 0xF" ::: "memory")
#define __ISB()  __asm volatile("isb 0xF" ::: "memory")

__STATIC_FORCEINLINE uint32_t __get_PRIMASK(void) {
    uint32_t result;
    __asm volatile("MRS %0, primask" : "=r"(result)::"memory");
    return result;
}

__STATIC_FORCEINLINE void __set_PRIMASK(uint32_t priMask) {
    __asm volatile("MSR primask, %0" : : "r"(priMask) : "memory");
}

__STATIC_FORCEINLINE void __disable_irq(void) {
    __asm volatile("cpsid i" : : : "memory");
}

__STATIC_FORCEINLINE void __enable_irq(void) {
    __asm volatile("cpsie i" : : : "memory");
}

#else /* host build */

#define __NOP()  ((void)0)
#define __WFE()  ((void)0)
#define __WFI()  ((void)0)
#define __SEV()  ((void)0)
#define __DSB()  __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define __DMB()  __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define __ISB()  __atomic_thread_fence(__ATOMIC_SEQ_CST)

__STATIC_FORCEINLINE uint32_t __get_PRIMASK(void) { return 0U; }
__STATIC_FORCEINLINE void __set_PRIMASK(uint32_t priMask) { (void)priMask; }
__STATIC_FORCEINLINE void __disable_irq(void) {}
__STATIC_FORCEINLINE void __enable_irq(void) {}

#endif /* M-profile */

#endif /* NSX_SMOKE_CMSIS_COMPILER_H */
