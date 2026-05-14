/*
 * SPDX-FileCopyrightText: Copyright 2026 Ambiq Micro, Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * CMSIS-based data-cache coherency overrides for the Arm Ethos-U
 * core driver. These replace the weak no-op defaults shipped by
 * upstream; see ethos-u-core-driver/README.md ("Data caching").
 *
 * Both addresses are required by the upstream contract to be 32-byte
 * aligned. Applications are still strongly encouraged to perform
 * their own IFM flush before invoking inference rather than relying
 * on `ethosu_flush_dcache`, which upstream documents as deprecated.
 */

#include <stddef.h>
#include <stdint.h>

/*
 * CMSIS core header — selected per-CPU by nsx-cmsis-core. On targets
 * without a unified-cache D-cache (e.g. Cortex-M4 builds that somehow
 * end up linking this TU), the SCB_*DCache_by_Addr helpers expand to
 * empty inline functions, so the body remains safe.
 */
#include "cmsis_compiler.h"

#if defined(__ARM_ARCH_8_1M_MAIN__) || defined(ARMCM55) || defined(ARMCM85)
#include "core_cm55.h" /* drags in SCB_CleanDCache_by_Addr et al. */
#else
/*
 * Fallback: rely on whatever CMSIS device header the board has already
 * pulled in (NSX_BOARD_FLAGS_TARGET adds the right one transparently).
 */
#endif

/*
 * The upstream symbols are declared `extern "C"` and weak in
 * `ethosu_driver.h`. Our strong definitions here win the link.
 */

void ethosu_flush_dcache(uint32_t *p, size_t bytes) {
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if (p != NULL && bytes > 0U) {
        SCB_CleanDCache_by_Addr(p, (int32_t)bytes);
    }
#else
    (void)p;
    (void)bytes;
#endif
}

void ethosu_invalidate_dcache(uint32_t *p, size_t bytes) {
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if (p != NULL && bytes > 0U) {
        SCB_InvalidateDCache_by_Addr(p, (int32_t)bytes);
    }
#else
    (void)p;
    (void)bytes;
#endif
}
