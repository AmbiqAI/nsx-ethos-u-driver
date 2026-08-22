/*
 * SPDX-FileCopyrightText: Copyright 2026 Ambiq Micro, Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Identity implementation of `ethosu_address_remap`. SoCs whose NPU
 * sees memory through a different aperture than the CPU (for example
 * an Atomiq-class part with an in-package DRAM window) should provide
 * a *strong* definition of this symbol in their BSP (we deliberately
 * define this in its own TU so the BSP can replace it wholesale rather
 * than having to override a function colocated with other definitions).
 *
 * This definition is `weak`, unlike the other overrides in src/. The
 * module's CMakeLists puts these TUs on every consumer's link line as
 * plain object files (see its "LINK SEMANTICS" note), so a strong
 * definition here would turn a BSP's own strong definition into a
 * duplicate-symbol error rather than an override. Weak keeps the
 * documented BSP extension point working, and costs nothing: the only
 * other definition in the link is upstream's own weak identity remap in
 * ethosu_device_uXX.c, which is semantically the same function.
 */

#include <stdint.h>

__attribute__((weak)) uint64_t ethosu_address_remap(uint64_t address, int index) {
    (void)index;
    return address;
}
