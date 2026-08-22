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
 * Concurrency model: take/give are ISR-safe. Mutual exclusion is a PRIMASK
 * critical section rather than LDREX/STREX, so the code makes no assumption
 * about the exclusive monitor (TCM, XIP-from-MRAM and non-shareable regions
 * all behave). Critical sections are a handful of instructions long.
 *
 * `ethosu_semaphore_create()` / `ethosu_semaphore_destroy()` are NOT ISR-safe
 * and are expected to be called from a single initialisation/teardown
 * context, which is how upstream uses them (`ethosu_init` / `ethosu_deinit`).
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
 * Test-and-sleep, fused so there is no lost-wakeup window.
 *
 * __WFE() executes with interrupts masked: an interrupt arriving here still
 * wakes the core (it becomes pending and sets the event register) but does not
 * run its handler until __set_PRIMASK() re-enables. So the sequence is
 * "observe count == 0, sleep, wake, run the ISR that gives the semaphore, look
 * again" -- the give can never slip between the check and the sleep.
 */
static int nsx_sem_try_take_or_sleep(struct nsx_ethos_u_sem *s) {
    uint32_t primask = nsx_sem_crit_enter();
    if (s->count > 0U) {
        s->count--;
        nsx_sem_crit_exit(primask);
        return 1;
    }
    __WFE();
    nsx_sem_crit_exit(primask);
    return 0;
}

static inline void nsx_sem_pause(void) {
    __NOP();
}

static inline void nsx_sem_signal_event(void) {
    __SEV();
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

void *ethosu_semaphore_create(void) {
    uint32_t primask = nsx_sem_crit_enter();

    for (size_t i = 0U; i < (size_t)NSX_ETHOSU_SEM_POOL_SIZE; i++) {
        if (!nsx_ethos_u_sem_pool[i].in_use) {
            nsx_ethos_u_sem_pool[i].in_use = 1U;
            nsx_ethos_u_sem_pool[i].count = 0U;
            nsx_sem_crit_exit(primask);
            return &nsx_ethos_u_sem_pool[i];
        }
    }

    nsx_sem_crit_exit(primask);
    /* Pool exhausted. Upstream treats a NULL handle as an init failure;
     * raise NSX_ETHOSU_SEM_POOL_SIZE if you legitimately need more. */
    return NULL;
}

void ethosu_semaphore_destroy(void *sem) {
    struct nsx_ethos_u_sem *s = (struct nsx_ethos_u_sem *)sem;

    if (s == NULL) {
        return;
    }

    uint32_t primask = nsx_sem_crit_enter();
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
            span = nsx_sem_timeout_to_ticks(timeout, ticks_per_ms);
            start = nsx_ethos_u_ticks();
            bounded = (span != UINT64_MAX);
        }
        /* ticks_per_ms == 0 -> no application timebase. Fall through to an
         * unbounded wait, i.e. exactly upstream's behaviour. */
    }

    for (;;) {
        if (bounded) {
#if NSX_ETHOSU_SEM_BOUNDED_WFE
            if (nsx_sem_try_take_or_sleep(s)) {
                return 0;
            }
#else
            if (nsx_sem_try_take(s)) {
                return 0;
            }
            nsx_sem_pause();
#endif
            /* Unsigned subtraction, so a wrapping timebase is handled as long
             * as the wait is shorter than the counter's period. */
            if ((nsx_ethos_u_ticks() - start) >= span) {
                /* One last non-blocking attempt: closes the race where the
                 * give landed between the failed take and this check. */
                if (nsx_sem_try_take(s)) {
                    return 0;
                }
                return -1;
            }
        } else {
            if (nsx_sem_try_take_or_sleep(s)) {
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
    (void)__atomic_fetch_add(&s->count, 1U, __ATOMIC_RELEASE);
#endif

    /* Wake any core parked in __WFE(). Must happen outside the critical
     * section so the event is visible the moment the waiter unmasks. */
    nsx_sem_signal_event();
    return 0;
}
