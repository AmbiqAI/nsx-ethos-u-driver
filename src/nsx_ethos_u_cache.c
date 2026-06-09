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
 * Pull in the SoC device header rather than a generic CMSIS core header so
 * feature macros like __DSP_PRESENT, __DCACHE_PRESENT, __NVIC_PRIO_BITS, and
 * IRQn_Type are defined before the core cache helpers are seen.
 */
#include "am_mcu_apollo.h"

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
