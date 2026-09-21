/*
 * SPDX-FileCopyrightText: Copyright 2026 Ambiq Micro, Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * CMSIS-based data-cache coherency overrides for the Arm Ethos-U
 * core driver. These replace the weak no-op defaults shipped by
 * upstream; see ethos-u-core-driver/README.md ("Data caching").
 *
 * Since driver 2.0.0 the hooks receive the job's whole base-pointer table
 * (address + size per region) once per inference instead of one call per
 * region, and the command stream is no longer passed separately: it lives
 * in the model's constant data, which the CPU never writes after boot.
 * Every populated region is cleaned before dispatch and invalidated after
 * completion, which is what the pre-2.0.0 driver did on our behalf. The
 * CMSIS SCB helpers round coverage out to whole 32-byte cache lines.
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

void ethosu_flush_dcache(const uint64_t *base_addr, const size_t *base_addr_size, int num_base_addr) {
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if (base_addr == NULL || base_addr_size == NULL) {
        return;
    }
    for (int i = 0; i < num_base_addr; i++) {
        if (base_addr[i] != 0U && base_addr_size[i] > 0U) {
            SCB_CleanDCache_by_Addr((void *)(uintptr_t)base_addr[i], (int32_t)base_addr_size[i]);
        }
    }
#else
    (void)base_addr;
    (void)base_addr_size;
    (void)num_base_addr;
#endif
}

void ethosu_invalidate_dcache(const uint64_t *base_addr, const size_t *base_addr_size, int num_base_addr) {
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if (base_addr == NULL || base_addr_size == NULL) {
        return;
    }
    for (int i = 0; i < num_base_addr; i++) {
        if (base_addr[i] != 0U && base_addr_size[i] > 0U) {
            SCB_InvalidateDCache_by_Addr((void *)(uintptr_t)base_addr[i], (int32_t)base_addr_size[i]);
        }
    }
#else
    (void)base_addr;
    (void)base_addr_size;
    (void)num_base_addr;
#endif
}
