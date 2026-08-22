/*
 * SPDX-FileCopyrightText: Copyright 2026 Ambiq Micro, Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bare-metal counting-semaphore overrides for the Arm Ethos-U core driver.
 *
 * Upstream ships weak bare-metal defaults in ethosu_driver.c. They have two
 * properties we do not want on Apollo:
 *
 *   1. `ethosu_semaphore_create()` calls malloc(). Bringing the heap into the
 *      NPU init path is unwelcome on bare-metal targets and makes the failure
 *      mode (silent NULL handle) hard to see.
 *
 *   2. `ethosu_semaphore_take()` ignores its `timeout` argument entirely and
 *      spins on `__WFE()` forever. That disables the whole recovery path in
 *      `ethosu_wait()`: a wedged NPU (no completion interrupt) hangs the
 *      caller instead of returning ETHOSU_JOB_RESULT_TIMEOUT and triggering
 *      `ethosu_soft_reset()`.
 *
 * These strong definitions replace both. The storage is a fixed static pool,
 * and the timeout is honoured against an optional application-supplied
 * timebase (see nsx_ethos_u_ticks / nsx_ethos_u_ticks_per_ms in
 * includes-api/nsx_ethos_u.h).
 *
 * ZERO BEHAVIOUR CHANGE BY DEFAULT. The weak default of
 * `nsx_ethos_u_ticks_per_ms()` returns 0, meaning "no time source". With no
 * time source every wait is unbounded, which is exactly what upstream does.
 * Integrators only get timeout enforcement once they deliberately supply both
 * hooks *and* a finite ETHOSU_SEMAPHORE_WAIT_INFERENCE.
 *
 * Concurrency model:
 *
 *   - `ethosu_semaphore_give()` is ISR-safe. That is the important one: the
 *     NPU completion interrupt gives the semaphore.
 *   - `ethosu_semaphore_take()` is ISR-safe only on its non-blocking path
 *     (count > 0, or a zero timeout with a live timebase). Its BLOCKING path
 *     must be called with interrupts ENABLED, from thread context. It parks in
 *     `__WFE()` with PRIMASK clear and relies on the giving ISR actually
 *     running; called with interrupts already masked it would spin or sleep
 *     until an unmasked exception happened along.
 *   - `ethosu_semaphore_create()` / `ethosu_semaphore_destroy()` are NOT
 *     ISR-safe and are expected to be called from a single
 *     initialisation/teardown context, which is how upstream uses them
 *     (`ethosu_init` / `ethosu_deinit`).
 *
 * Mutual exclusion is a PRIMASK critical section rather than LDREX/STREX, so
 * the code makes no assumption about the exclusive monitor (TCM, XIP-from-MRAM
 * and non-shareable regions all behave). Critical sections are a handful of
 * instructions long.
 */

#include "nsx_ethos_u.h"

#include "ethosu_driver.h"

#include <stddef.h>
#include <stdint.h>

/*
 * M-profile detection. Deliberately NOT `defined(__ARM_ARCH)`: an aarch64
 * build host defines that too but has no PRIMASK and no Cortex-M event
 * register. Everything else (host builds, unit tests) takes the portable
 * path so this TU stays compilable off-target.
 */
#if defined(__ARM_ARCH_PROFILE) && (__ARM_ARCH_PROFILE == 'M')
#define NSX_ETHOSU_SEM_CORTEX_M 1
#else
#define NSX_ETHOSU_SEM_CORTEX_M 0
#endif

#if NSX_ETHOSU_SEM_CORTEX_M
/* Same rationale as nsx_ethos_u_cache.c: pull the SoC header, not a bare
 * CMSIS core header, so feature macros are set before the intrinsics. */
#include "am_mcu_apollo.h"
#else
#include <time.h>
#endif

/*
 * Number of semaphores the pool can hand out. Upstream allocates one global
 * semaphore for NPU arbitration plus one per `ethosu_driver` instance, so the
 * default covers three NPUs. Override from the build system if needed.
 */
#ifndef NSX_ETHOSU_SEM_POOL_SIZE
#define NSX_ETHOSU_SEM_POOL_SIZE 4
#endif

/*
 * Wake strategy for *bounded* waits (finite timeout AND a time source):
 *
 *   0 (default) - poll the timebase. Costs cycles, but the deadline is
 *                 enforced unconditionally. This matters: the case a timeout
 *                 exists to catch is "the NPU never raises its interrupt", and
 *                 in that case there may be no wake event at all.
 *   1           - sleep on __WFE() between checks. Lower power, but only
 *                 correct if some other interrupt (e.g. the SysTick backing
 *                 nsx_ethos_u_ticks()) fires at least as often as the
 *                 deadline resolution you care about. Opt in only if you
 *                 know that holds.
 *
 * Unbounded waits always use __WFE(); there is no deadline to miss.
 */
#ifndef NSX_ETHOSU_SEM_BOUNDED_WFE
#define NSX_ETHOSU_SEM_BOUNDED_WFE 0
#endif

struct nsx_ethos_u_sem {
    volatile uint32_t count;
    /* Number of callers currently inside the blocking part of take(). Read by
     * destroy() so it can refuse to recycle a slot somebody is parked on. */
    volatile uint32_t waiters;
    volatile uint8_t in_use;
};

static struct nsx_ethos_u_sem nsx_ethos_u_sem_pool[NSX_ETHOSU_SEM_POOL_SIZE];

/* ------------------------------------------------------------------------
 * Weak timebase hooks. See includes-api/nsx_ethos_u.h for the contract.
 * ------------------------------------------------------------------------ */

__attribute__((weak)) uint64_t nsx_ethos_u_ticks(void) {
    return 0U;
}

__attribute__((weak)) uint32_t nsx_ethos_u_ticks_per_ms(void) {
    return 0U;
}

/* ------------------------------------------------------------------------
 * Platform primitives
 * ------------------------------------------------------------------------ */

#if NSX_ETHOSU_SEM_CORTEX_M

static inline uint32_t nsx_sem_crit_enter(void) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static inline void nsx_sem_crit_exit(uint32_t primask) {
    __set_PRIMASK(primask);
}

static int nsx_sem_try_take(struct nsx_ethos_u_sem *s) {
    uint32_t primask = nsx_sem_crit_enter();
    if (s->count > 0U) {
        s->count--;
        nsx_sem_crit_exit(primask);
        return 1;
    }
    nsx_sem_crit_exit(primask);
    return 0;
}

/*
 * Test-and-sleep. The lost-wakeup window is closed by the event register, not
 * by the critical section.
 *
 * The __WFE() deliberately executes AFTER PRIMASK is restored. Executing it
 * *inside* the critical section is a hang: per Armv8-M (B3.24, "Wait For
 * Event") only WFI treats a pending-but-masked interrupt as a wake-up event;
 * WFE does not, unless SCB->SCR.SEVONPEND is set. With PRIMASK=1 and SEVONPEND
 * clear, an NPU completion interrupt arriving between the count check and the
 * WFE leaves the core asleep forever -- and that is the *default* path
 * (no timebase hooks => unbounded wait).
 *
 * What makes the check->sleep window safe instead is the event register latch:
 * ethosu_semaphore_give() executes __SEV() after incrementing, which SETS the
 * event register. WFE consumes-and-returns immediately if the event register is
 * already set. So:
 *
 *   give lands before crit_exit  -> impossible; PRIMASK is set, the ISR is
 *                                   deferred to crit_exit.
 *   give lands after crit_exit,
 *   before WFE                   -> __SEV() latches the event; WFE returns at
 *                                   once and the caller re-checks count.
 *   give lands during WFE        -> __SEV() (and the interrupt itself) wakes it.
 *
 * A stale event register (a __SEV() whose token we already consumed) only costs
 * one extra spin of the caller's loop.
 */
static int nsx_sem_try_take_or_sleep(struct nsx_ethos_u_sem *s) {
    uint32_t primask = nsx_sem_crit_enter();
    if (s->count > 0U) {
        s->count--;
        nsx_sem_crit_exit(primask);
        return 1;
    }
    nsx_sem_crit_exit(primask);
    __WFE();
    return 0;
}

static inline void nsx_sem_pause(void) {
    __NOP();
}

static inline void nsx_sem_signal_event(void) {
    __SEV();
}

static void nsx_sem_waiters_inc(struct nsx_ethos_u_sem *s) {
    uint32_t primask = nsx_sem_crit_enter();
    s->waiters++;
    nsx_sem_crit_exit(primask);
}

static void nsx_sem_waiters_dec(struct nsx_ethos_u_sem *s) {
    uint32_t primask = nsx_sem_crit_enter();
    s->waiters--;
    nsx_sem_crit_exit(primask);
}

#else /* portable / host path */

static inline uint32_t nsx_sem_crit_enter(void) {
    return 0U;
}

static inline void nsx_sem_crit_exit(uint32_t primask) {
    (void)primask;
}

static int nsx_sem_try_take(struct nsx_ethos_u_sem *s) {
    uint32_t c = __atomic_load_n(&s->count, __ATOMIC_ACQUIRE);
    while (c > 0U) {
        /* CAS reloads `c` on failure, so a concurrent take just retries. */
        if (__atomic_compare_exchange_n(&s->count, &c, c - 1U, 1, __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) {
            return 1;
        }
    }
    return 0;
}

/* No event register off-target; yield the CPU briefly instead of spinning
 * hot, so host unit tests do not peg a core. */
static inline void nsx_sem_pause(void) {
    struct timespec ts = {0, 100000L}; /* 100 us */
    (void)nanosleep(&ts, NULL);
}

static int nsx_sem_try_take_or_sleep(struct nsx_ethos_u_sem *s) {
    if (nsx_sem_try_take(s)) {
        return 1;
    }
    nsx_sem_pause();
    return 0;
}

static inline void nsx_sem_signal_event(void) {}

static void nsx_sem_waiters_inc(struct nsx_ethos_u_sem *s) {
    (void)__atomic_fetch_add(&s->waiters, 1U, __ATOMIC_ACQ_REL);
}

static void nsx_sem_waiters_dec(struct nsx_ethos_u_sem *s) {
    (void)__atomic_fetch_sub(&s->waiters, 1U, __ATOMIC_ACQ_REL);
}

#endif /* NSX_ETHOSU_SEM_CORTEX_M */

/* ------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------ */

/*
 * Convert a millisecond timeout to timebase ticks, saturating rather than
 * wrapping. A saturated span is indistinguishable from "wait forever", which
 * is the safe direction to err for an absurdly large timeout.
 */
static uint64_t nsx_sem_timeout_to_ticks(uint64_t timeout_ms, uint32_t ticks_per_ms) {
    if (timeout_ms > (UINT64_MAX / (uint64_t)ticks_per_ms)) {
        return UINT64_MAX;
    }
    return timeout_ms * (uint64_t)ticks_per_ms;
}

/* ------------------------------------------------------------------------
 * Strong overrides of upstream's weak bare-metal implementations
 * ------------------------------------------------------------------------ */

/*
 * NOTE (host/portable path only): nsx_sem_crit_enter() is a no-op off-target,
 * so the claim of a free slot below is not atomic there. That is deliberate --
 * upstream calls create() from single-threaded init (`ethosu_init`), and the
 * host build exists to exercise take/give semantics, not to be a general
 * threadsafe allocator. On Cortex-M the PRIMASK critical section does make the
 * scan-and-claim atomic.
 */
void *ethosu_semaphore_create(void) {
    uint32_t primask = nsx_sem_crit_enter();

    for (size_t i = 0U; i < (size_t)NSX_ETHOSU_SEM_POOL_SIZE; i++) {
        if (!nsx_ethos_u_sem_pool[i].in_use) {
            nsx_ethos_u_sem_pool[i].in_use = 1U;
            nsx_ethos_u_sem_pool[i].count = 0U;
            nsx_ethos_u_sem_pool[i].waiters = 0U;
            nsx_sem_crit_exit(primask);
            return &nsx_ethos_u_sem_pool[i];
        }
    }

    nsx_sem_crit_exit(primask);
    /* Pool exhausted. Upstream treats a NULL handle as an init failure;
     * raise NSX_ETHOSU_SEM_POOL_SIZE if you legitimately need more. */
    return NULL;
}

/*
 * Destroying a semaphore somebody is still blocked on is API misuse (upstream
 * only calls this from ethosu_deinit(), after every job has been reaped). If it
 * happens anyway we must NOT recycle the slot: a later create() would hand the
 * same storage to a new owner, and the still-parked waiter would consume the
 * new owner's first give(). That failure -- one inference silently stealing
 * another's completion token -- is far worse than leaking a pool entry.
 *
 * Behaviour, therefore: with waiters != 0 the slot is left permanently in use
 * (count is *not* cleared either, so the parked waiter can still complete) and
 * this call is otherwise a no-op. The upstream signature is void, so there is
 * no way to report it; the leak shows up as create() returning NULL once the
 * pool drains, which upstream surfaces as an init failure.
 */
void ethosu_semaphore_destroy(void *sem) {
    struct nsx_ethos_u_sem *s = (struct nsx_ethos_u_sem *)sem;

    if (s == NULL) {
        return;
    }

    uint32_t primask = nsx_sem_crit_enter();
    if (s->waiters != 0U) {
        nsx_sem_crit_exit(primask);
        return;
    }
    s->count = 0U;
    s->in_use = 0U;
    nsx_sem_crit_exit(primask);
}

/*
 * @param timeout  ETHOSU_SEMAPHORE_WAIT_FOREVER, or a timeout in
 *                 MILLISECONDS. Upstream defines the unit as
 *                 implementation-defined and passes the value through
 *                 untouched; milliseconds is what NSX picks.
 * @return 0 if the semaphore was taken, -1 on timeout or bad handle.
 *
 * The blocking path must be entered with interrupts enabled -- see the
 * concurrency note at the top of this file.
 */
int ethosu_semaphore_take(void *sem, uint64_t timeout) {
    struct nsx_ethos_u_sem *s = (struct nsx_ethos_u_sem *)sem;

    if (s == NULL) {
        return -1;
    }

    int bounded = 0;
    uint64_t start = 0U;
    uint64_t span = 0U;

    if (timeout != ETHOSU_SEMAPHORE_WAIT_FOREVER) {
        const uint32_t ticks_per_ms = nsx_ethos_u_ticks_per_ms();
        if (ticks_per_ms > 0U) {
            /*
             * Guard against a half-provided timebase: an integrator who
             * overrides ticks_per_ms() but leaves ticks() on its weak
             * return-0 default would otherwise get a bounded loop whose clock
             * never advances -- an unbounded 100%-CPU spin that also never
             * times out, which is strictly worse than either alternative.
             *
             * Detection is deliberately crude: sample twice and treat "both
             * reads are zero" as a dead source. A real counter reads zero at
             * most for a few ticks after reset, and the fallback (unbounded
             * wait) is upstream's own behaviour, so a false positive costs
             * nothing but the timeout for one call. Movement between the two
             * reads is not required -- a slow tick legitimately repeats.
             */
            const uint64_t probe_a = nsx_ethos_u_ticks();
            const uint64_t probe_b = nsx_ethos_u_ticks();
            if ((probe_a | probe_b) != 0U) {
                span = nsx_sem_timeout_to_ticks(timeout, ticks_per_ms);
                start = probe_b;
                bounded = (span != UINT64_MAX);
            }
        }
        /* ticks_per_ms == 0 (or a dead ticks()) -> no application timebase.
         * Fall through to an unbounded wait, i.e. exactly upstream's
         * behaviour. */
    }

    nsx_sem_waiters_inc(s);

    for (;;) {
        if (bounded) {
#if NSX_ETHOSU_SEM_BOUNDED_WFE
            if (nsx_sem_try_take_or_sleep(s)) {
                nsx_sem_waiters_dec(s);
                return 0;
            }
#else
            if (nsx_sem_try_take(s)) {
                nsx_sem_waiters_dec(s);
                return 0;
            }
            nsx_sem_pause();
#endif
            /* Unsigned subtraction on a counter the contract requires to wrap
             * only at 2^64 (see includes-api/nsx_ethos_u.h): a narrower
             * hardware counter must be software-extended by the integrator,
             * otherwise its wrap looks like a huge elapsed time and fires a
             * spurious timeout. */
            if ((nsx_ethos_u_ticks() - start) >= span) {
                /* One last non-blocking attempt: closes the race where the
                 * give landed between the failed take and this check. */
                if (nsx_sem_try_take(s)) {
                    nsx_sem_waiters_dec(s);
                    return 0;
                }
                nsx_sem_waiters_dec(s);
                return -1;
            }
        } else {
            if (nsx_sem_try_take_or_sleep(s)) {
                nsx_sem_waiters_dec(s);
                return 0;
            }
        }
    }
}

int ethosu_semaphore_give(void *sem) {
    struct nsx_ethos_u_sem *s = (struct nsx_ethos_u_sem *)sem;

    if (s == NULL) {
        return -1;
    }

#if NSX_ETHOSU_SEM_CORTEX_M
    uint32_t primask = nsx_sem_crit_enter();
    if (s->count == UINT32_MAX) {
        nsx_sem_crit_exit(primask);
        return -1;
    }
    s->count++;
    nsx_sem_crit_exit(primask);
#else
    /* CAS loop rather than fetch_add so the host path saturates exactly like
     * the Cortex-M path above: at UINT32_MAX we report failure instead of
     * wrapping the count to zero and losing every outstanding token. */
    uint32_t c = __atomic_load_n(&s->count, __ATOMIC_RELAXED);
    do {
        if (c == UINT32_MAX) {
            return -1;
        }
    } while (!__atomic_compare_exchange_n(&s->count, &c, c + 1U, 1, __ATOMIC_RELEASE,
                                          __ATOMIC_RELAXED));
#endif

    /* Wake any core parked in __WFE(). Must happen outside the critical
     * section so the event is visible the moment the waiter unmasks. */
    nsx_sem_signal_event();
    return 0;
}

#ifdef NSX_ETHOSU_SEM_TEST_ACCESS
/*
 * Test-only seam. Defined solely for the host unit tests (which compile this
 * TU directly and define NSX_ETHOSU_SEM_TEST_ACCESS); never compiled into a
 * shipping build. It exists so a test can park the count next to UINT32_MAX
 * and check the give() saturation edge without four billion give() calls.
 */
void nsx_ethos_u_sem_test_set_count(void *sem, uint32_t count);

void nsx_ethos_u_sem_test_set_count(void *sem, uint32_t count) {
    struct nsx_ethos_u_sem *s = (struct nsx_ethos_u_sem *)sem;
    if (s != NULL) {
        s->count = count;
    }
}
#endif
