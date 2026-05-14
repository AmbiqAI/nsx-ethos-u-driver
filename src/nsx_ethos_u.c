/*
 * SPDX-FileCopyrightText: Copyright 2026 Ambiq Micro, Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "nsx_ethos_u.h"

#include "ethosu_driver.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Single-handle convenience layer.
 *
 * Stores the most recently initialised driver so the board's vector
 * table can dispatch the NPU IRQ to a fixed C symbol
 * (`nsx_ethos_u_irq`) without having to know about the handle.
 *
 * Apps that manage their own driver handle (or multiple NPUs) should
 * skip these helpers and call `ethosu_init()` / `ethosu_irq_handler()`
 * directly.
 */
static struct ethosu_driver *g_nsx_ethos_u_drv;

int nsx_ethos_u_init(struct ethosu_driver *drv, void *npu_base, uint32_t irq_num) {
    (void)irq_num; /* informational only; board owns the vector wiring */
    if (drv == NULL || npu_base == NULL) {
        return -1;
    }

    int rc = ethosu_init(drv,
                         npu_base,
                         /* fast_memory      */ NULL,
                         /* fast_memory_size */ 0U,
                         /* secure_enable    */ 1U,
                         /* privilege_enable */ 1U);
    if (rc == 0) {
        g_nsx_ethos_u_drv = drv;
    }
    return rc;
}

void nsx_ethos_u_irq(void) {
    if (g_nsx_ethos_u_drv != NULL) {
        ethosu_irq_handler(g_nsx_ethos_u_drv);
    }
}
