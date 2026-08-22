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
 *
 * WHERE THE BSP'S STRONG DEFINITION HAS TO LIVE
 *
 * A strong definition only wins if the linker actually *sees* it, and the
 * failure mode when it does not is silent: the weak identity below stays, the
 * NPU is handed CPU-view addresses, and the link succeeds. So the BSP's
 * definition must be either
 *
 *   - an object file named directly on the final link line (an OBJECT library
 *     attached to the executable, or a plain .o), or
 *   - inside a static-archive member that is extracted anyway because some
 *     *other* symbol in that same TU is undefined and referenced (e.g. the
 *     board init function the application calls).
 *
 * A TU that contains nothing but ethosu_address_remap() and is dropped into a
 * static archive is dead: nothing in the link is left undefined by it (this
 * file and upstream both already define the symbol weakly), so the member is
 * never extracted and the override never happens. This is the same archive
 * extraction trap documented at length in the module's CMakeLists.txt
 * ("LINK SEMANTICS"), and the reason the overrides here are an OBJECT library.
 *
 * Verify on a real image, not by inspection:
 *     arm-none-eabi-nm app.elf | grep ethosu_address_remap
 * 'T' means a strong definition won; 'W'/'V' means an identity remap is live.
 */

#include <stdint.h>

__attribute__((weak)) uint64_t ethosu_address_remap(uint64_t address, int index) {
    (void)index;
    return address;
}
