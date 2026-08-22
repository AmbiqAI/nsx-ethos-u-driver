/*
 * SPDX-FileCopyrightText: Copyright 2026 Ambiq Micro, Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * The middle layer of the three-level override acceptance: a STATIC library
 * that consumes nsx::ethos_u_driver and re-exports it to its own dependents.
 * This is what a real BSP / HAL wrapper looks like -- the application links
 * the wrapper, not the driver.
 *
 * Like link_acceptance.c it touches ONLY nsx_ethos_u_init(); it deliberately
 * references nothing from the override TUs, so the override objects have to
 * arrive on the final link line under their own steam.
 */

#include "link_middle.h"

#include "nsx_ethos_u.h"

#include "ethosu_driver.h"

static struct ethosu_driver nsx_smoke_middle_drv;

int nsx_smoke_middle_init(void *npu_base, uint32_t irq_num) {
    return nsx_ethos_u_init(&nsx_smoke_middle_drv, npu_base, irq_num);
}
