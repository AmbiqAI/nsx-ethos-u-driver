/*
 * SPDX-FileCopyrightText: Copyright 2026 Ambiq Micro, Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * SMOKE-BUILD STUB -- NOT FOR PRODUCTION USE.
 *
 * The NSX glue in src/ pulls in the SoC device header (rather than a bare
 * CMSIS core header) so that feature macros such as __DCACHE_PRESENT and the
 * SCB cache helpers are visible. The real header ships with the Ambiq SDK and
 * is not vendored here, so the CI build-smoke harness supplies this stand-in.
 *
 * __DCACHE_PRESENT is deliberately 1 so that the *real* branch of
 * src/nsx_ethos_u_cache.c is the one that gets compiled; the SCB helpers below
 * are no-op inlines. This is a compile smoke test, not a functional one.
 */

#ifndef NSX_SMOKE_AM_MCU_APOLLO_H
#define NSX_SMOKE_AM_MCU_APOLLO_H

#include <stddef.h>
#include <stdint.h>

#include "cmsis_compiler.h"

/* Core feature macros normally provided by the device's core_cm55.h. */
#ifndef __DCACHE_PRESENT
#define __DCACHE_PRESENT 1U
#endif
#ifndef __ICACHE_PRESENT
#define __ICACHE_PRESENT 1U
#endif
#ifndef __DSP_PRESENT
#define __DSP_PRESENT 1U
#endif
#ifndef __FPU_PRESENT
#define __FPU_PRESENT 1U
#endif
#ifndef __MPU_PRESENT
#define __MPU_PRESENT 1U
#endif
#ifndef __NVIC_PRIO_BITS
#define __NVIC_PRIO_BITS 3U
#endif

/*
 * Minimal IRQn_Type. Only the negative Cortex-M exception numbers plus a
 * placeholder device IRQ are needed to satisfy NVIC prototypes.
 */
typedef enum {
    NonMaskableInt_IRQn = -14,
    HardFault_IRQn      = -13,
    SVCall_IRQn         = -5,
    PendSV_IRQn         = -2,
    SysTick_IRQn        = -1,
    NSX_SMOKE_IRQn      = 0
} IRQn_Type;

__STATIC_FORCEINLINE void NVIC_EnableIRQ(IRQn_Type IRQn) { (void)IRQn; }
__STATIC_FORCEINLINE void NVIC_DisableIRQ(IRQn_Type IRQn) { (void)IRQn; }
__STATIC_FORCEINLINE void NVIC_ClearPendingIRQ(IRQn_Type IRQn) { (void)IRQn; }

/* SCB data-cache maintenance stubs (see __DCACHE_PRESENT note above). */
__STATIC_FORCEINLINE void SCB_CleanDCache(void) {}
__STATIC_FORCEINLINE void SCB_InvalidateDCache(void) {}
__STATIC_FORCEINLINE void SCB_CleanInvalidateDCache(void) {}
__STATIC_FORCEINLINE void SCB_CleanDCache_by_Addr(void *addr, int32_t dsize) {
    (void)addr;
    (void)dsize;
}
__STATIC_FORCEINLINE void SCB_InvalidateDCache_by_Addr(void *addr, int32_t dsize) {
    (void)addr;
    (void)dsize;
}
__STATIC_FORCEINLINE void SCB_CleanInvalidateDCache_by_Addr(void *addr, int32_t dsize) {
    (void)addr;
    (void)dsize;
}

#endif /* NSX_SMOKE_AM_MCU_APOLLO_H */
