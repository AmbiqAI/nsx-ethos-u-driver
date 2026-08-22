/*
 * SPDX-FileCopyrightText: Copyright 2026 Ambiq Micro, Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host unit tests for src/nsx_ethos_u_semaphore.c.
 *
 * The TU under test is compiled straight into this executable (not linked
 * from the archive) so the portable non-Cortex-M path is exercised natively.
 * The Cortex-M path gets compile coverage from the arm-none-eabi smoke job.
 *
 * Two link configurations are built from this one source:
 *
 *   NSX_SEM_TEST_WITH_HOOKS defined  - this file supplies strong
 *       nsx_ethos_u_ticks / nsx_ethos_u_ticks_per_ms, overriding the weak
 *       defaults in the TU. Exercises real timeout enforcement, and doubles
 *       as a regression test that the weak-override mechanism the whole
 *       module depends on actually links the way we think it does.
 *
 *   NSX_SEM_TEST_WITH_HOOKS undefined - the weak defaults stay in place, so
 *       ticks_per_ms() == 0 ("no time source"). Asserts the zero-behaviour-
 *       change promise: finite timeouts degrade to waiting forever.
 */

#include "nsx_ethos_u.h"

#include "ethosu_driver.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int failures = 0;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (cond) {                                                            \
            printf("  ok   : " __VA_ARGS__);                                   \
            printf("\n");                                                      \
        } else {                                                               \
            printf("  FAIL : " __VA_ARGS__);                                   \
            printf("   (%s:%d: %s)\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

/* ------------------------------------------------------------------------
 * Host timebase
 * ------------------------------------------------------------------------ */

static uint64_t host_micros(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000L);
}

#ifdef NSX_SEM_TEST_WITH_HOOKS
/* Strong overrides of the weak hooks in nsx_ethos_u_semaphore.c. */
uint64_t nsx_ethos_u_ticks(void) {
    return host_micros();
}

uint32_t nsx_ethos_u_ticks_per_ms(void) {
    return 1000U; /* microsecond timebase */
}
#endif

/* ------------------------------------------------------------------------
 * Helper: give the semaphore from another thread after a delay
 * ------------------------------------------------------------------------ */

struct delayed_give {
    void *sem;
    unsigned delay_us;
};

static void *delayed_give_thread(void *arg) {
    struct delayed_give *dg = (struct delayed_give *)arg;
    struct timespec ts = {0, 0};
    ts.tv_nsec = (long)dg->delay_us * 1000L;
    nanosleep(&ts, NULL);
    ethosu_semaphore_give(dg->sem);
    return NULL;
}

/* ------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------ */

/* (a) give-then-take succeeds immediately, and counts rather than latches. */
static void test_give_then_take(void) {
    printf("test_give_then_take\n");

    void *sem = ethosu_semaphore_create();
    CHECK(sem != NULL, "create returns a handle");
    if (sem == NULL) {
        return;
    }

    CHECK(ethosu_semaphore_give(sem) == 0, "give succeeds");

    uint64_t t0 = host_micros();
    CHECK(ethosu_semaphore_take(sem, ETHOSU_SEMAPHORE_WAIT_FOREVER) == 0,
          "take after give succeeds");
    CHECK((host_micros() - t0) < 50000ULL, "take returned promptly (<50 ms)");

    /* Counting, not binary: two gives must satisfy two takes. */
    CHECK(ethosu_semaphore_give(sem) == 0, "second give succeeds");
    CHECK(ethosu_semaphore_give(sem) == 0, "third give succeeds");
    CHECK(ethosu_semaphore_take(sem, ETHOSU_SEMAPHORE_WAIT_FOREVER) == 0,
          "first of two counted takes succeeds");
    CHECK(ethosu_semaphore_take(sem, ETHOSU_SEMAPHORE_WAIT_FOREVER) == 0,
          "second of two counted takes succeeds");

    ethosu_semaphore_destroy(sem);
}

/*
 * (b) A finite timeout with a timebase available and nothing giving must fail
 *     after roughly the requested interval. Without hooks the same call must
 *     block, so it is only meaningful in the hooks build.
 */
#ifdef NSX_SEM_TEST_WITH_HOOKS
static void test_take_times_out(void) {
    printf("test_take_times_out\n");

    void *sem = ethosu_semaphore_create();
    CHECK(sem != NULL, "create returns a handle");
    if (sem == NULL) {
        return;
    }

    const uint64_t timeout_ms = 50U;
    uint64_t t0 = host_micros();
    int ret = ethosu_semaphore_take(sem, timeout_ms);
    uint64_t elapsed_us = host_micros() - t0;

    CHECK(ret < 0, "take with no giver returns failure (%d)", ret);
    CHECK(elapsed_us >= timeout_ms * 1000ULL,
          "waited at least the timeout (%llu us >= %llu us)",
          (unsigned long long)elapsed_us, (unsigned long long)(timeout_ms * 1000ULL));
    /* Generous upper bound: CI runners are noisy, we only care that the wait
     * is bounded at all rather than hanging. */
    CHECK(elapsed_us < 5ULL * timeout_ms * 1000ULL,
          "did not overshoot wildly (%llu us < %llu us)",
          (unsigned long long)elapsed_us,
          (unsigned long long)(5ULL * timeout_ms * 1000ULL));

    ethosu_semaphore_destroy(sem);
}

/* A give that lands before the deadline must win over the timeout. */
static void test_take_give_before_deadline(void) {
    printf("test_take_give_before_deadline\n");

    void *sem = ethosu_semaphore_create();
    CHECK(sem != NULL, "create returns a handle");
    if (sem == NULL) {
        return;
    }

    struct delayed_give dg = {sem, 20000U}; /* 20 ms */
    pthread_t th;
    CHECK(pthread_create(&th, NULL, delayed_give_thread, &dg) == 0,
          "spawned giver thread");

    uint64_t t0 = host_micros();
    int ret = ethosu_semaphore_take(sem, 2000U); /* 2 s deadline */
    uint64_t elapsed_us = host_micros() - t0;

    pthread_join(th, NULL);

    CHECK(ret == 0, "take succeeded before the deadline (%d)", ret);
    CHECK(elapsed_us < 1000000ULL, "returned well inside the deadline (%llu us)",
          (unsigned long long)elapsed_us);

    ethosu_semaphore_destroy(sem);
}
#endif /* NSX_SEM_TEST_WITH_HOOKS */

/*
 * (c) With no time source (weak hook defaults), a finite timeout must still
 *     behave like upstream: wait until somebody gives. Run in both builds --
 *     in the hooks build it uses WAIT_FOREVER to cover the unbounded path.
 */
static void test_take_blocks_until_give(void) {
    printf("test_take_blocks_until_give\n");

#ifdef NSX_SEM_TEST_WITH_HOOKS
    const uint64_t timeout = ETHOSU_SEMAPHORE_WAIT_FOREVER;
    const char *what = "WAIT_FOREVER";
#else
    /* Deliberately finite. With ticks_per_ms() == 0 this must NOT time out;
     * that is the zero-behaviour-change guarantee. */
    const uint64_t timeout = 20U;
    const char *what = "finite timeout, no time source";
#endif

    void *sem = ethosu_semaphore_create();
    CHECK(sem != NULL, "create returns a handle");
    if (sem == NULL) {
        return;
    }

    struct delayed_give dg = {sem, 60000U}; /* 60 ms -- past the 20 ms above */
    pthread_t th;
    CHECK(pthread_create(&th, NULL, delayed_give_thread, &dg) == 0,
          "spawned giver thread");

    uint64_t t0 = host_micros();
    int ret = ethosu_semaphore_take(sem, timeout);
    uint64_t elapsed_us = host_micros() - t0;

    pthread_join(th, NULL);

    CHECK(ret == 0, "take (%s) succeeded once given (%d)", what, ret);
    CHECK(elapsed_us >= 50000ULL, "actually waited for the giver (%llu us)",
          (unsigned long long)elapsed_us);

    ethosu_semaphore_destroy(sem);
}

/* Pool accounting: handles must be recycled by destroy(), and exhaustion must
 * report NULL rather than returning a bogus handle. */
static void test_pool_recycling(void) {
    printf("test_pool_recycling\n");

    void *first = ethosu_semaphore_create();
    CHECK(first != NULL, "create returns a handle");

    void *held[64];
    int n = 0;
    while (n < 64) {
        void *s = ethosu_semaphore_create();
        if (s == NULL) {
            break;
        }
        held[n++] = s;
    }
    CHECK(n < 64, "pool is finite and exhaustion returns NULL (%d extra)", n);

    for (int i = 0; i < n; i++) {
        ethosu_semaphore_destroy(held[i]);
    }
    ethosu_semaphore_destroy(first);

    void *again = ethosu_semaphore_create();
    CHECK(again != NULL, "handles are recycled after destroy");
#ifdef NSX_SEM_TEST_WITH_HOOKS
    /* Only safe with a timebase: without one a finite timeout is unbounded
     * by design, so this take would block forever. */
    CHECK(ethosu_semaphore_take(again, 0U) != 0,
          "recycled handle starts with count 0");
#endif
    ethosu_semaphore_destroy(again);
}

/* Defensive: NULL handles must not fault. */
static void test_null_handle(void) {
    printf("test_null_handle\n");
    CHECK(ethosu_semaphore_take(NULL, ETHOSU_SEMAPHORE_WAIT_FOREVER) != 0,
          "take(NULL) reports failure instead of hanging");
    CHECK(ethosu_semaphore_give(NULL) != 0, "give(NULL) reports failure");
    ethosu_semaphore_destroy(NULL); /* must not crash */
    CHECK(1, "destroy(NULL) is a no-op");
}

int main(void) {
#ifdef NSX_SEM_TEST_WITH_HOOKS
    printf("nsx_ethos_u_semaphore tests [timebase hooks provided]\n");
    CHECK(nsx_ethos_u_ticks_per_ms() == 1000U, "hook override is linked in");
#else
    printf("nsx_ethos_u_semaphore tests [weak defaults, no time source]\n");
    CHECK(nsx_ethos_u_ticks_per_ms() == 0U, "weak default reports no time source");
    CHECK(nsx_ethos_u_ticks() == 0U, "weak default tick source reads 0");
#endif

    test_give_then_take();
#ifdef NSX_SEM_TEST_WITH_HOOKS
    test_take_times_out();
    test_take_give_before_deadline();
#endif
    test_take_blocks_until_give();
    test_pool_recycling();
    test_null_handle();

    if (failures != 0) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
