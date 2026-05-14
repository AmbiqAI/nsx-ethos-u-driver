/*
 * SPDX-FileCopyrightText: Copyright 2026 Ambiq Micro, Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Identity implementation of `ethosu_address_remap`. SoCs whose NPU
 * sees memory through a different aperture than the CPU (for example
 * an Atomiq-class part with an in-package DRAM window) should provide
 * a *strong* definition of this symbol in their BSP and link it
 * *after* this object — the linker will then prefer the BSP version
 * (we deliberately define this in its own TU so the BSP can replace
 * it wholesale rather than having to override a function colocated
 * with other definitions).
 */

#include <stdint.h>

uint64_t ethosu_address_remap(uint64_t address, int index) {
    (void)index;
    return address;
}
