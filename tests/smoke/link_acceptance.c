/*
 * SPDX-FileCopyrightText: Copyright 2026 Ambiq Micro, Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal "what would a real application drag in?" translation unit, used for
 * the override acceptance check (tests/smoke/check_overrides.cmake).
 *
 * It references ONLY ethosu_init() and nsx_ethos_u_init() -- deliberately
 * nothing from src/nsx_ethos_u_cache.c, src/nsx_ethos_u_callbacks.c or
 * src/nsx_ethos_u_semaphore.c. Referencing those would force their archive
 * members in and mask exactly the bug under test: with the override TUs as
 * plain archive members nothing ever left them undefined, so they were never
 * extracted and upstream's weak malloc()-based semaphore and no-op cache
 * maintenance quietly won the link.
 *
 * No libc, no startup code: this builds and (partially) links under
 * arm-none-eabi without a linker script, so the same check runs cross.
 */

#include "nsx_ethos_u.h"

#include "ethosu_driver.h"

/*
 * Volatile + externally visible so the compiler cannot fold the references
 * away, and so no dead-strip pass can remove them before the linker has
 * resolved them.
 */
typedef void (*nsx_link_acceptance_fn)(void);

volatile nsx_link_acceptance_fn nsx_link_acceptance_refs[] = {
    (nsx_link_acceptance_fn)(void *)&ethosu_init,
    (nsx_link_acceptance_fn)(void *)&nsx_ethos_u_init,
};
