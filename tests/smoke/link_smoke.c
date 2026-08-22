/*
 * SPDX-FileCopyrightText: Copyright 2026 Ambiq Micro, Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Link-time smoke check for the nsx-ethos-u-driver static library.
 *
 * Takes the address of a representative symbol from each TU in the archive so
 * the linker is forced to pull every object in and resolve it. Nothing here
 * touches real hardware.
 */

#include "nsx_ethos_u.h"

#include "ethosu_driver.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* One entry point per TU that the module contributes to the archive. */
static const uintptr_t referenced[] = {
    /* external/ethos-u-core-driver/src/ethosu_driver.c */
    (uintptr_t)&ethosu_init,
    (uintptr_t)&ethosu_invoke_v3,
    (uintptr_t)&ethosu_wait,
    /* src/nsx_ethos_u.c */
    (uintptr_t)&nsx_ethos_u_init,
    (uintptr_t)&nsx_ethos_u_irq,
    /* src/nsx_ethos_u_cache.c */
    (uintptr_t)&ethosu_flush_dcache,
    (uintptr_t)&ethosu_invalidate_dcache,
    /* src/nsx_ethos_u_callbacks.c */
    (uintptr_t)&nsx_ethos_u_set_probe,
    /* src/nsx_ethos_u_remap.c */
    (uintptr_t)&ethosu_address_remap,
    /* src/nsx_ethos_u_semaphore.c */
    (uintptr_t)&ethosu_semaphore_create,
    (uintptr_t)&ethosu_semaphore_destroy,
    (uintptr_t)&ethosu_semaphore_take,
    (uintptr_t)&ethosu_semaphore_give,
    (uintptr_t)&nsx_ethos_u_ticks,
    (uintptr_t)&nsx_ethos_u_ticks_per_ms,
};

int main(void) {
    const size_t n = sizeof(referenced) / sizeof(referenced[0]);
    size_t resolved = 0;

    for (size_t i = 0; i < n; i++) {
        if (referenced[i] != 0U) {
            resolved++;
        }
    }

    printf("nsx-ethos-u-driver link smoke: %zu/%zu symbols resolved\n", resolved, n);
    return (resolved == n) ? 0 : 1;
}
