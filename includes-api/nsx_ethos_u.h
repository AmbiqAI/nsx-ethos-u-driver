/*
 * SPDX-FileCopyrightText: Copyright 2026 Ambiq Micro, Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file nsx_ethos_u.h
 * @brief NSX-flavoured facade around Arm's ethos-u-core-driver.
 *
 * This header is intentionally thin. The upstream driver API
 * (`ethosu_driver.h`) remains first-class — consumers may call any
 * `ethosu_*` function directly. The helpers here provide:
 *
 *   - a one-call init that matches NSX conventions,
 *   - a board-side IRQ trampoline,
 *   - an optional probe hook so profilers (e.g. helia-profiler) can
 *     observe inference begin/end without owning the weak overrides.
 *
 * Boards are responsible for:
 *   - supplying the NPU MMIO base address and IRQ number,
 *   - dispatching the NPU IRQ to `nsx_ethos_u_irq()` from the vector
 *     table (or directly to `ethosu_irq_handler()` if they prefer to
 *     manage the driver handle themselves),
 *   - optionally overriding `ethosu_address_remap()` for SoCs whose
 *     base-pointer aperture differs from the CPU view (e.g. DRAM).
 */

#ifndef NSX_ETHOS_U_H
#define NSX_ETHOS_U_H

#include "ethosu_driver.h" /* upstream API */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Phase identifiers passed to @ref nsx_ethos_u_probe_fn. */
#define NSX_ETHOS_U_PROBE_BEGIN "begin"
#define NSX_ETHOS_U_PROBE_END   "end"

/**
 * Optional probe callback invoked from the default weak overrides of
 * `ethosu_inference_begin` / `ethosu_inference_end`.
 *
 * @param phase    One of NSX_ETHOS_U_PROBE_BEGIN / NSX_ETHOS_U_PROBE_END.
 * @param drv      The driver handle for this inference.
 * @param user_arg The user_arg pointer passed to `ethosu_invoke_v3()`.
 */
typedef void (*nsx_ethos_u_probe_fn)(const char *phase,
                                     struct ethosu_driver *drv,
                                     void *user_arg);

/**
 * Register (or clear, with NULL) the probe callback. Single-slot;
 * later calls replace earlier ones.
 */
void nsx_ethos_u_set_probe(nsx_ethos_u_probe_fn fn);

/**
 * Initialise an Ethos-U driver instance.
 *
 * Convenience wrapper around `ethosu_init()` that supplies the
 * common-case parameters for U85 (no fast-memory spill area, secure
 * + privileged) and stashes the handle for `nsx_ethos_u_irq()`.
 *
 * For finer control (fast memory, non-secure mode, multiple NPUs),
 * call `ethosu_init()` directly.
 *
 * @param drv       Caller-allocated driver handle (zero-initialised).
 * @param npu_base  NPU register base address (board-specific).
 * @param irq_num   NVIC IRQ number for the NPU (board-specific). Used
 *                  only for documentation and runtime checks; the
 *                  board must still wire the vector entry to
 *                  `nsx_ethos_u_irq()` (or `ethosu_irq_handler()`).
 * @return 0 on success, otherwise the negative error code from
 *         `ethosu_init()`.
 */
int nsx_ethos_u_init(struct ethosu_driver *drv,
                     void *npu_base,
                     uint32_t irq_num);

/**
 * IRQ trampoline. Boards hook this into their vector table at the
 * NPU IRQ slot. It dispatches to `ethosu_irq_handler()` with the
 * handle most recently passed to `nsx_ethos_u_init()`.
 *
 * Boards that manage multiple Ethos-U devices, or that prefer to
 * carry their own driver handle, should skip this helper and call
 * `ethosu_irq_handler(drv)` directly.
 */
void nsx_ethos_u_irq(void);

#ifdef __cplusplus
}
#endif

#endif /* NSX_ETHOS_U_H */
