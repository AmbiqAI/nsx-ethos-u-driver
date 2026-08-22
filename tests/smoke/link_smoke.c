/*
 * SPDX-FileCopyrightText: Copyright 2026 Ambiq Micro, Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Link-time smoke check for the nsx-ethos-u-driver static library.
 *
 * This used to take the address of one symbol per TU, which proved only that
 * every symbol resolved to *something*. It could not distinguish "our strong
 * override is in the image" from "upstream's weak default is in the image",
 * and taking those addresses actively forced the override archive members in,
 * hiding the real bug (the override TUs were never extracted from the archive
 * in a normal application link).
 *
 * So: the reference list below names ONLY entry points a plain application
 * uses. Everything else is checked by *behaviour* -- each assertion below is
 * chosen so that upstream's weak default gives a different answer from ours.
 *
 * Nothing here touches real hardware.
 */

#include "nsx_ethos_u.h"

#include "ethosu_driver.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* What a real application actually calls. Nothing defined by the override TUs
 * (cache, callbacks, remap, semaphore) appears here on purpose. */
static const uintptr_t referenced[] = {
    /* external/ethos-u-core-driver/src/ethosu_driver.c */
    (uintptr_t)&ethosu_init,
    (uintptr_t)&ethosu_invoke_v3,
    (uintptr_t)&ethosu_wait,
    /* src/nsx_ethos_u.c */
    (uintptr_t)&nsx_ethos_u_init,
    (uintptr_t)&nsx_ethos_u_irq,
};

static int failures = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (cond) {                                                            \
            printf("  ok   : %s\n", (msg));                                    \
        } else {                                                               \
            printf("  FAIL : %s   (%s:%d)\n", (msg), __FILE__, __LINE__);      \
            failures++;                                                        \
        }                                                                      \
    } while (0)

/*
 * Upstream's weak ethosu_semaphore_create() is a malloc() wrapper: it has no
 * upper bound and hands back heap blocks forever. Ours draws from a static
 * pool of NSX_ETHOSU_SEM_POOL_SIZE (4) entries and returns NULL when drained.
 * Exhaustion is therefore a clean, allocator-independent proof of which
 * implementation the linker actually put in this binary.
 */
static void check_semaphore_override(void) {
    printf("semaphore override (static pool, not upstream malloc)\n");

    void *held[64];
    int n = 0;

    while (n < 64) {
        void *s = ethosu_semaphore_create();
        if (s == NULL) {
            break;
        }
        held[n++] = s;
    }

    CHECK(n > 0, "create() hands out at least one semaphore");
    CHECK(n < 64,
          "create() exhausts a finite pool -- upstream's malloc() version "
          "would never return NULL");

    if (n > 0) {
        /* Counting behaviour of the pool implementation. */
        CHECK(ethosu_semaphore_give(held[0]) == 0, "give() on a pool handle succeeds");
        CHECK(ethosu_semaphore_take(held[0], ETHOSU_SEMAPHORE_WAIT_FOREVER) == 0,
              "take() consumes the token given above");
    }

    for (int i = 0; i < n; i++) {
        ethosu_semaphore_destroy(held[i]);
    }

    /* Slots must come back, otherwise the pool leaked. */
    void *again = ethosu_semaphore_create();
    CHECK(again != NULL, "destroy() recycles pool slots");
    ethosu_semaphore_destroy(again);
}

/*
 * Upstream's weak ethosu_inference_begin/end are no-ops. src/nsx_ethos_u_callbacks.c
 * replaces them with a fan-out to nsx_ethos_u_set_probe(). If the probe fires,
 * that TU is in the image.
 */
static int probe_hits;

static void probe_cb(const char *phase, struct ethosu_driver *drv, void *user_arg) {
    (void)phase;
    (void)drv;
    (void)user_arg;
    probe_hits++;
}

static void check_callbacks_override(void) {
    printf("callbacks override (probe fan-out, not upstream no-op)\n");

    nsx_ethos_u_set_probe(probe_cb);
    ethosu_inference_begin(NULL, NULL);
    ethosu_inference_end(NULL, NULL);
    nsx_ethos_u_set_probe(NULL);

    CHECK(probe_hits == 2,
          "begin/end dispatch to the NSX probe -- upstream's weak versions "
          "are no-ops");
}

int main(void) {
    const size_t n = sizeof(referenced) / sizeof(referenced[0]);
    size_t resolved = 0;

    for (size_t i = 0; i < n; i++) {
        if (referenced[i] != 0U) {
            resolved++;
        }
    }

    printf("nsx-ethos-u-driver link smoke: %zu/%zu application entry points resolved\n",
           resolved, n);
    CHECK(resolved == n, "every application entry point resolved");

    check_semaphore_override();
    check_callbacks_override();

    if (failures != 0) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
