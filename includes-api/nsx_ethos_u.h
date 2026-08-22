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
 *
 * @note Overriding `ethosu_address_remap()` has a linkage requirement. The
 *       BSP's strong definition must reach the final link line as an OBJECT
 *       file (or sit in a static-archive member that is extracted anyway
 *       because the application references some other symbol from that same
 *       TU). A strong definition alone in an archive member is never
 *       extracted -- nothing in the link is undefined without it, since both
 *       this module and upstream define the symbol weakly -- so the weak
 *       identity remap silently wins and the NPU sees CPU-view addresses with
 *       no link error. See `src/nsx_ethos_u_remap.c` for the details and for
 *       the `nm` one-liner that checks a real image.
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
 * @param irq_num   NVIC IRQ number for the NPU (board-specific).
 *                  Informational only (unused); the board must still
 *                  wire the vector entry to `nsx_ethos_u_irq()` (or
 *                  `ethosu_irq_handler()`).
 * @return 0 on success, otherwise the negative error code from
 *         `ethosu_init()`.
 */
int nsx_ethos_u_init(struct ethosu_driver *drv,
                     void *npu_base,
                     uint32_t irq_num);

/**
 * As @ref nsx_ethos_u_init, with explicit security/privilege selection.
 *
 * The values must match the security/privilege level the NPU actually
 * observes on its interface (PROT.active_CSL/CPL) -- which is a platform
 * property, not necessarily the CPU-side alias used for MMIO. The values
 * map directly
 * to `ethosu_init()`'s `secure_enable` / `privilege_enable` parameters,
 * which program RESET.pending_CSL/CPL and are verified against
 * PROT.active_CSL/CPL by the upstream driver.
 */
int nsx_ethos_u_init_ex(struct ethosu_driver *drv,
                        void *npu_base,
                        uint32_t irq_num,
                        uint32_t secure_enable,
                        uint32_t privilege_enable);

/**
 * Clear the driver handle stashed for @ref nsx_ethos_u_irq.
 *
 * Call during teardown from thread context (not from an ISR that may
 * have preempted a running NPU handler), AFTER masking the NPU IRQ at the NVIC
 * (`NVIC_DisableIRQ` + `__DSB()`/`__ISB()`) and BEFORE
 * `ethosu_deinit(drv)`: a late or already-pended interrupt then no-ops
 * in `nsx_ethos_u_irq()` instead of dispatching into a freed driver.
 */
void nsx_ethos_u_deinit(void);

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

/**
 * @name Optional timebase hooks for the NPU wait semaphore
 *
 * `src/nsx_ethos_u_semaphore.c` provides strong overrides of upstream's weak
 * bare-metal `ethosu_semaphore_*` functions. Unlike upstream's, ours honours
 * the `timeout` argument -- but only if the application tells it how to
 * measure time, via the two weak hooks below.
 *
 * ## Contract
 *
 * - `nsx_ethos_u_ticks()` returns a free-running, monotonic 64-bit tick count
 *   that **wraps only at 2^64**. The wait loop measures elapsed time as an
 *   unsigned difference, so a counter that wraps sooner reports a huge elapsed
 *   span at every wrap and fires spurious timeouts. Hardware counters narrower
 *   than 64 bits (and counters that count *down*) therefore must NOT be
 *   returned raw: software-extend them, e.g. accumulate a 64-bit total in the
 *   counter's own overflow/reload interrupt and add the current sub-count.
 *   At a realistic tick rate 2^64 ticks is longer than the part will ever be
 *   powered, so this makes wraparound a non-issue rather than a tolerated one.
 *
 *   It is called from the semaphore wait loop and must be safe to call from
 *   any context and must not block.
 *
 * - `nsx_ethos_u_ticks()` must **never return 0 once the timebase is
 *   running**. 0 is the "no timebase" sentinel: the wait loop samples it twice
 *   at entry and reads two zeros as "no time source", degrading to an
 *   unbounded wait. A counter that is legitimately 0 for the first few ticks
 *   after reset (or that is read before its clock is enabled) would therefore
 *   silently disable every inference timeout. Bias the value so it cannot be
 *   zero, e.g. `return counter | 1;` or `return counter + 1;` -- both cost one
 *   instruction and neither perturbs elapsed-time differences.
 *
 * - `nsx_ethos_u_ticks_per_ms()` returns the tick rate in ticks per
 *   millisecond, or **0 to declare that no time source exists**.
 *
 * - Supply **both hooks or neither**. Overriding only `ticks_per_ms()` is
 *   detected (the wait samples `ticks()` twice at entry and treats two zero
 *   reads as a dead time source) and degrades to an unbounded wait rather than
 *   spinning forever at 100% CPU on a clock that never advances -- but do not
 *   rely on that; it is a safety net, not a supported configuration.
 *
 * - The `timeout` passed to `ethosu_semaphore_take()` is interpreted in
 *   **milliseconds**. Upstream leaves the unit implementation-defined and
 *   ships the value through untouched; NSX picks milliseconds.
 *
 * ## Setting the inference deadline
 *
 * The deadline `ethosu_wait()` uses is the `ETHOSU_SEMAPHORE_WAIT_INFERENCE`
 * macro, which upstream's `ethosu_driver.c` consumes when *the library* is
 * compiled. A `#define` in application code is therefore inert. Set it from
 * the build system instead:
 *
 *     cmake -DNSX_ETHOSU_INFERENCE_TIMEOUT_MS=2000 ...
 *
 * Empty (the default) leaves upstream's `ETHOSU_SEMAPHORE_WAIT_FOREVER`.
 *
 * ## Default behaviour
 *
 * Both hooks have weak definitions returning 0. With `ticks_per_ms() == 0`
 * every wait is unbounded, which reproduces upstream's behaviour exactly --
 * so integrators without a timebase see no change. Define either or both
 * symbols in application code to override.
 *
 * Once a timebase is supplied *and* `NSX_ETHOSU_INFERENCE_TIMEOUT_MS` is
 * finite, a stalled NPU makes `ethosu_semaphore_take()` return -1 after the
 * deadline, which is what lets `ethosu_wait()` mark the job
 * ETHOSU_JOB_RESULT_TIMEOUT and issue `ethosu_soft_reset()`.
 *
 * ## Caveats
 *
 * - The rate is integral, so a sub-kHz timebase rounds down: a 32768 Hz
 *   counter is 32 ticks/ms, making timeouts fire ~2.4% early. Scale the
 *   counter in `nsx_ethos_u_ticks()` if that matters.
 * - Bounded waits poll the timebase by default rather than sleeping on
 *   `__WFE()`, because the failure they exist to catch (NPU never raises its
 *   interrupt) may come with no wake event at all. Build with
 *   `-DNSX_ETHOSU_SEM_BOUNDED_WFE=1` to sleep instead, if you know another
 *   interrupt fires often enough.
 * - Semaphore handles come from a static pool sized by
 *   `NSX_ETHOSU_SEM_POOL_SIZE` (default 4: one global + one per NPU).
 * - `ethosu_semaphore_take()`'s blocking path parks in `__WFE()` and must be
 *   called from thread context with interrupts enabled; `ethosu_semaphore_give()`
 *   is ISR-safe.
 * @{
 */

/**
 * @return Monotonic tick count that wraps only at 2^64, and never 0 once the
 *         timebase is running (0 is the no-timebase sentinel, and is what the
 *         weak default returns). Software-extend any narrower hardware
 *         counter.
 */
uint64_t nsx_ethos_u_ticks(void);

/** @return Ticks per millisecond, or 0 for "no time source" (weak default). */
uint32_t nsx_ethos_u_ticks_per_ms(void);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* NSX_ETHOS_U_H */
