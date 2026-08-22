/*
 * SPDX-FileCopyrightText: Copyright 2026 Ambiq Micro, Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * The application layer of the three-level override acceptance. It links the
 * middle library ONLY -- it never names nsx::ethos_u_driver, and it never
 * names an ethosu_* symbol. Everything it gets from the driver, including the
 * override objects, has to arrive transitively through the middle library's
 * link interface.
 *
 * That is the scenario the PRIVATE-linkage trap breaks: if the middle library
 * links the driver PRIVATE, the driver's INTERFACE_SOURCES (the override
 * objects) are wrapped in $<LINK_ONLY:> and never reach this link line, the
 * upstream weak hooks win, and nothing about the build fails. The POST_BUILD
 * check_overrides.cmake run on this target is what turns that into an error.
 *
 * No libc, no startup code: this is a partial (-r) link, so it also runs in
 * the arm-none-eabi cross build.
 */

#include "link_middle.h"

typedef int (*nsx_link_transitive_fn)(void *, uint32_t);

volatile nsx_link_transitive_fn nsx_link_transitive_refs[] = {
    &nsx_smoke_middle_init,
};
