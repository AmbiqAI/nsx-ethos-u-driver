/*
 * SPDX-FileCopyrightText: Copyright 2026 Ambiq Micro, Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Default begin/end inference callbacks. These override the weak
 * no-ops in the upstream driver and fan out to a single, optional
 * NSX-side probe callback. helia-profiler (and any other profiler)
 * uses `nsx_ethos_u_set_probe()` to subscribe.
 */

#include "nsx_ethos_u.h"

#include "ethosu_driver.h"

#include <stddef.h>

static nsx_ethos_u_probe_fn g_probe;

void nsx_ethos_u_set_probe(nsx_ethos_u_probe_fn fn) {
    g_probe = fn;
}

void ethosu_inference_begin(struct ethosu_driver *drv, void *user_arg) {
    nsx_ethos_u_probe_fn p = g_probe;
    if (p != NULL) {
        p(NSX_ETHOS_U_PROBE_BEGIN, drv, user_arg);
    }
}

void ethosu_inference_end(struct ethosu_driver *drv, void *user_arg) {
    nsx_ethos_u_probe_fn p = g_probe;
    if (p != NULL) {
        p(NSX_ETHOS_U_PROBE_END, drv, user_arg);
    }
}
