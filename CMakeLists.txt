# SPDX-FileCopyrightText: Copyright 2026 Ambiq Micro, Inc.
# SPDX-License-Identifier: Apache-2.0
#
# nsx-ethos-u-driver — NSX integration of Arm's ethos-u-core-driver.
#
# Layout:
#   external/ethos-u-core-driver/   - upstream driver, vendored as plain
#                                     files (Apache-2.0; see PROVENANCE.md).
#                                     We compile its
#                                     sources directly with NSX toolchain
#                                     flags rather than calling
#                                     add_subdirectory() on its CMakeLists.
#   includes-api/                   - public NSX-flavoured facade.
#   src/                            - weak-symbol overrides (cache, IRQ
#                                     trampoline, begin/end probes, default
#                                     identity address remap, counting
#                                     semaphore with real timeout support).
#
# LINK SEMANTICS -- why the overrides are an OBJECT library
#
#   Upstream *defines* its bare-metal hooks (ethosu_semaphore_*, ethosu_*cache,
#   ethosu_inference_begin/end, ethosu_address_remap) as weak symbols inside
#   ethosu_driver.c / ethosu_device_uXX.c. Our strong replacements live in
#   separate TUs in src/.
#
#   If those TUs are ordinary members of a STATIC archive, they are DEAD CODE:
#   a linker extracts an archive member only to satisfy an *undefined* symbol.
#   ethosu_driver.c.obj is always extracted (the app calls ethosu_init), and it
#   already carries weak definitions of every hook, so nothing is ever left
#   undefined and src/nsx_ethos_u_semaphore.c.obj & friends are never pulled in.
#   The upstream malloc()-based semaphore and no-op cache maintenance win, and
#   nothing about the link fails loudly. Reproduce the old behaviour with:
#       arm-none-eabi-ld -r app.o libnsx_ethos_u_driver.a   # -> only 3 members
#
#   Fix: the override TUs are compiled into an OBJECT library whose object
#   files are attached to `nsx_ethos_u_driver`'s link interface. Object files
#   named on a link line are unconditionally included, so the strong
#   definitions are always present and beat the archive's weak ones. This is
#   toolchain-independent (no -Wl,-u / --whole-archive / --undefined tricks,
#   which are gcc/armlink-specific and, for -u, would need per-TU anchor
#   symbols because -u on an *overridden* name is satisfied by the weak
#   definition in the already-extracted ethosu_driver.c.obj).
#
#   Consequence for non-CMake consumers: linking libnsx_ethos_u_driver.a on a
#   hand-written link line is NOT enough. Also name the installed override
#   objects (see the install() rules at the bottom) on the link line.
#
# Approach (B): explicit source listing.
#
#   Upstream's own CMakeLists.txt drives target-CPU selection via
#   `CMAKE_SYSTEM_PROCESSOR` (or `TARGET_CPU` when used in conjunction with
#   ethos-u-core-platform toolchain files). It also performs its own flag
#   manipulation and assumes a gcc-style driver. To remain aligned with the
#   NSX toolchain model (`nsx::soc_flags`, nsx_apply_toolchain_flags)
#   and to keep armclang / ATfE behaviour predictable, we deliberately do NOT
#   `add_subdirectory()` upstream. Instead we list its sources here and let
#   the active SoC-flags target carry the CPU/FPU/ABI flags transitively.
#
#   Upstream source still needs a compile-time NPU family selector, which we
#   provide as `ETHOSU_ARCH` plus the matching `ETHOSU55`/`ETHOSU65`/`ETHOSU85`
#   define (this is what `src/ethosu_interface.h` and the config headers
#   actually gate on). The per-NPU device backend (`ethosu_device_uXX.c`) is
#   selected from the family token below.
#
# If you ever need to re-evaluate this trade-off, the pivot point is whether
# upstream's CMakeLists begins to bake in flag generation we can't easily
# replicate. As of ethos-u-core-driver 25.x it does not.

if(NOT TARGET nsx::soc_flags)
    message(FATAL_ERROR
        "nsx::soc_flags must be defined before nsx-ethos-u-driver is added.")
endif()
if(NOT TARGET nsx::cmsis_core)
    message(FATAL_ERROR
        "nsx::cmsis_core must be defined before nsx-ethos-u-driver is added.")
endif()
if(NOT TARGET nsx::core)
    message(FATAL_ERROR
        "nsx::core must be defined before nsx-ethos-u-driver is added.")
endif()
if(NOT TARGET nsx::soc_hal)
    message(FATAL_ERROR
        "nsx::soc_hal must be defined before nsx-ethos-u-driver is added.")
endif()

# ----------------------------------------------------------------------------
# Configurables
# ----------------------------------------------------------------------------

set(NSX_ETHOSU_NPU_CONFIG "ethos-u85-256" CACHE STRING
    "Ethos-U target NPU config (e.g. ethos-u55-128, ethos-u65-256, ethos-u85-256)")

option(NSX_ETHOSU_BUILD_PMU
    "Build the Ethos-U PMU helper TU from upstream" ON)

# Upstream's ethosu_log.h defaults ETHOSU_LOG_ENABLE to 1, which pulls
# fprintf(stdout/stderr, ...) into the driver -- unwanted stdio /
# semi-hosting pressure on bare-metal builds. Default it off; consumers
# that want upstream logging can turn this on. PRIVATE below: ethosu_log.h
# lives in the driver's private src/ include dir, so no consumer TU sees it.
option(NSX_ETHOSU_LOG_ENABLE
    "Enable upstream ethos-u-core-driver LOG()/fprintf-based logging" OFF)

# Deadline used by upstream's ethosu_wait() when it blocks on the inference
# semaphore (ethosu_driver.c consumes ETHOSU_SEMAPHORE_WAIT_INFERENCE at *lib*
# compile time, so an app-side #define is inert -- it has to be set here).
#
# Empty (default) = leave upstream's default of ETHOSU_SEMAPHORE_WAIT_FOREVER
# in place. Set to a positive integer number of MILLISECONDS to bound the wait;
# that only takes effect once the application also supplies the
# nsx_ethos_u_ticks()/nsx_ethos_u_ticks_per_ms() timebase hooks (see
# includes-api/nsx_ethos_u.h) -- without them a finite value still waits
# forever, exactly as upstream does.
set(NSX_ETHOSU_INFERENCE_TIMEOUT_MS "" CACHE STRING
    "Inference-wait timeout in ms for ethosu_wait() (empty = wait forever)")

# Parse the family (u55 / u65 / u85) from the config string. Used both to
# pick the device backend source and to set per-family compile defines that
# upstream's headers gate on.
string(REGEX MATCH "^ethos-u([0-9]+)-([0-9]+)$" _NSX_ETHOSU_FAMILY_MATCH "${NSX_ETHOSU_NPU_CONFIG}")
if(NOT _NSX_ETHOSU_FAMILY_MATCH)
    message(FATAL_ERROR
        "nsx-ethos-u-driver: NSX_ETHOSU_NPU_CONFIG='${NSX_ETHOSU_NPU_CONFIG}' "
        "is not of the form 'ethos-uNN-MACS'.")
endif()
set(NSX_ETHOSU_FAMILY_NUM "${CMAKE_MATCH_1}")
set(NSX_ETHOSU_FAMILY "u${NSX_ETHOSU_FAMILY_NUM}")
set(NSX_ETHOSU_MACS "${CMAKE_MATCH_2}")

if(NSX_ETHOSU_FAMILY STREQUAL "u55")
    set(NSX_ETHOSU_DEVICE_DEFINE ETHOSU55)
elseif(NSX_ETHOSU_FAMILY STREQUAL "u65")
    set(NSX_ETHOSU_DEVICE_DEFINE ETHOSU65)
elseif(NSX_ETHOSU_FAMILY STREQUAL "u85")
    set(NSX_ETHOSU_DEVICE_DEFINE ETHOSU85)
else()
    message(FATAL_ERROR
        "nsx-ethos-u-driver: unsupported NPU family '${NSX_ETHOSU_FAMILY}' "
        "(from NSX_ETHOSU_NPU_CONFIG='${NSX_ETHOSU_NPU_CONFIG}'). "
        "Supported: u55, u65, u85.")
endif()

# ----------------------------------------------------------------------------
# Vendored upstream
# ----------------------------------------------------------------------------

set(_NSX_ETHOSU_VENDOR_DIR ${CMAKE_CURRENT_LIST_DIR}/external/ethos-u-core-driver)
set(_NSX_ETHOSU_VENDOR_INC ${_NSX_ETHOSU_VENDOR_DIR}/include)
set(_NSX_ETHOSU_VENDOR_SRC ${_NSX_ETHOSU_VENDOR_DIR}/src)

if(NOT EXISTS "${_NSX_ETHOSU_VENDOR_INC}/ethosu_driver.h")
    message(FATAL_ERROR
        "nsx-ethos-u-driver: vendored ethos-u-core-driver not found at "
        "${_NSX_ETHOSU_VENDOR_DIR}. The vendored tree ships in-repo; "
        "re-vendor it from the pinned revision in PROVENANCE.md.")
endif()

# Verified against ethos-u-core-driver tag 24.08, all .c (no .cc):
#   src/ethosu_driver.c
#   src/ethosu_device_u55_u65.c   (U55 and U65 share one backend)
#   src/ethosu_device_u85.c
#   src/ethosu_pmu.c
# If a future bump changes file extensions or adds a new device backend,
# update the list below.

if(NSX_ETHOSU_FAMILY STREQUAL "u55" OR NSX_ETHOSU_FAMILY STREQUAL "u65")
    set(_NSX_ETHOSU_DEVICE_SRC "${_NSX_ETHOSU_VENDOR_SRC}/ethosu_device_u55_u65.c")
elseif(NSX_ETHOSU_FAMILY STREQUAL "u85")
    set(_NSX_ETHOSU_DEVICE_SRC "${_NSX_ETHOSU_VENDOR_SRC}/ethosu_device_u85.c")
else()
    message(FATAL_ERROR
        "nsx-ethos-u-driver: unsupported NPU family '${NSX_ETHOSU_FAMILY}' "
        "(from NSX_ETHOSU_NPU_CONFIG='${NSX_ETHOSU_NPU_CONFIG}'). "
        "Supported: u55, u65, u85.")
endif()

set(_NSX_ETHOSU_UPSTREAM_SOURCES
    ${_NSX_ETHOSU_VENDOR_SRC}/ethosu_driver.c
    ${_NSX_ETHOSU_DEVICE_SRC}
)
if(NSX_ETHOSU_BUILD_PMU)
    list(APPEND _NSX_ETHOSU_UPSTREAM_SOURCES ${_NSX_ETHOSU_VENDOR_SRC}/ethosu_pmu.c)
endif()

foreach(_src IN LISTS _NSX_ETHOSU_UPSTREAM_SOURCES)
    if(NOT EXISTS "${_src}")
        message(FATAL_ERROR
            "nsx-ethos-u-driver: expected upstream source '${_src}' not found. "
            "Did the vendored upstream layout change?")
    endif()
endforeach()

# ----------------------------------------------------------------------------
# Library target
# ----------------------------------------------------------------------------

# Usage requirements shared by the archive and the override OBJECT library.
# Held in variables rather than a helper INTERFACE target so the exported
# target set stays exactly what it was.
set(_NSX_ETHOSU_PUBLIC_DEFS
    ETHOSU
    ETHOSU_ARCH=${NSX_ETHOSU_FAMILY}
    ETHOSU_MACS=${NSX_ETHOSU_MACS}
    ${NSX_ETHOSU_DEVICE_DEFINE}
    NSX_HAS_NPU=1
    NSX_ETHOSU_NPU_FAMILY_${NSX_ETHOSU_FAMILY}=1
)
if(NOT NSX_ETHOSU_INFERENCE_TIMEOUT_MS STREQUAL "")
    if(NOT NSX_ETHOSU_INFERENCE_TIMEOUT_MS MATCHES "^[0-9]+$")
        message(FATAL_ERROR
            "nsx-ethos-u-driver: NSX_ETHOSU_INFERENCE_TIMEOUT_MS="
            "'${NSX_ETHOSU_INFERENCE_TIMEOUT_MS}' must be a non-negative "
            "integer number of milliseconds (or empty for 'wait forever').")
    endif()
    # PUBLIC, not PRIVATE: ethosu_driver.c consumes the macro, but
    # ethosu_driver.h *defines* the fallback under #ifndef and is on the public
    # include path. A PRIVATE define would leave any consumer TU that includes
    # ethosu_driver.h reading WAIT_FOREVER while the library was compiled with
    # the bounded value -- a silent header/library disagreement.
    list(APPEND _NSX_ETHOSU_PUBLIC_DEFS
        ETHOSU_SEMAPHORE_WAIT_INFERENCE=${NSX_ETHOSU_INFERENCE_TIMEOUT_MS}ULL)
endif()

set(_NSX_ETHOSU_PUBLIC_INCS
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/includes-api>
    $<BUILD_INTERFACE:${_NSX_ETHOSU_VENDOR_INC}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/nsx/modules/nsx-ethos-u-driver/includes-api>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/nsx/modules/nsx-ethos-u-driver/ethos-u-core-driver/include>
)

set(_NSX_ETHOSU_DEPS
    nsx::soc_flags
    nsx::cmsis_core
    nsx::core
    nsx::soc_hal
)

# ---- Override TUs: OBJECT library (see "LINK SEMANTICS" at the top) --------
#
# These four TUs each provide a *strong* definition that must displace an
# upstream weak one. They are deliberately NOT archive members.
set(_NSX_ETHOSU_OVERRIDE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/nsx_ethos_u_cache.c
    ${CMAKE_CURRENT_LIST_DIR}/src/nsx_ethos_u_callbacks.c
    ${CMAKE_CURRENT_LIST_DIR}/src/nsx_ethos_u_remap.c
    ${CMAKE_CURRENT_LIST_DIR}/src/nsx_ethos_u_semaphore.c
)
add_library(nsx_ethos_u_overrides OBJECT ${_NSX_ETHOSU_OVERRIDE_SOURCES})
# No EXPORT_NAME / install(TARGETS): this target is not part of nsxTargets. Its
# objects reach installed consumers as plain files (install(FILES) below) named
# from nsx_ethos_u_driver's INSTALL_INTERFACE sources. The alias exists so a
# BSP can reach the target to add flags, not so anything links it.
add_library(nsx::ethos_u_overrides ALIAS nsx_ethos_u_overrides)
target_compile_definitions(nsx_ethos_u_overrides PRIVATE ${_NSX_ETHOSU_PUBLIC_DEFS})
target_include_directories(nsx_ethos_u_overrides PRIVATE ${_NSX_ETHOSU_PUBLIC_INCS})
target_link_libraries(nsx_ethos_u_overrides PRIVATE ${_NSX_ETHOSU_DEPS})

# ---- The archive ----------------------------------------------------------

add_library(nsx_ethos_u_driver STATIC
    ${_NSX_ETHOSU_UPSTREAM_SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/src/nsx_ethos_u.c
)
set_target_properties(nsx_ethos_u_driver PROPERTIES EXPORT_NAME ethos_u_driver)
add_library(nsx::ethos_u_driver ALIAS nsx_ethos_u_driver)

# Attach the override objects to the *link interface*, i.e. to every consumer's
# link line, and deliberately NOT to the archive itself.
#
#   * `target_link_libraries(... PUBLIC nsx_ethos_u_overrides)` is NOT enough:
#     CMake resolves that by dropping the objects into libnsx_ethos_u_driver.a,
#     where they are back to being never-extracted dead members.
#   * Exporting the OBJECT library and naming it in INTERFACE_LINK_LIBRARIES is
#     also NOT enough: an IMPORTED OBJECT library reached through a link
#     interface contributes nothing to the consumer's link line either.
#   * INTERFACE_SOURCES carrying object-file paths is what actually works. The
#     build-tree half uses $<TARGET_OBJECTS:...>; the install-tree half names
#     the objects at their installed location (install(FILES) below puts them
#     there under names we control, rather than the generator-dependent
#     objects[-CONFIG]/<target>/ layout install(TARGETS ... OBJECTS) uses).
set(_NSX_ETHOSU_OBJ_INSTALL_DIR
    ${CMAKE_INSTALL_LIBDIR}/nsx/modules/nsx-ethos-u-driver/objects)
set(_NSX_ETHOSU_INSTALLED_OBJECTS "")
foreach(_src IN LISTS _NSX_ETHOSU_OVERRIDE_SOURCES)
    get_filename_component(_obj_name "${_src}" NAME)
    list(APPEND _NSX_ETHOSU_INSTALLED_OBJECTS
        "$<INSTALL_INTERFACE:${_NSX_ETHOSU_OBJ_INSTALL_DIR}/${_obj_name}${CMAKE_C_OUTPUT_EXTENSION}>")
endforeach()

target_sources(nsx_ethos_u_driver INTERFACE
    $<BUILD_INTERFACE:$<TARGET_OBJECTS:nsx_ethos_u_overrides>>
    ${_NSX_ETHOSU_INSTALLED_OBJECTS}
)
# Consumers reference the build-tree objects by path, so make sure they exist
# before anything links against the archive.
add_dependencies(nsx_ethos_u_driver nsx_ethos_u_overrides)

target_compile_definitions(nsx_ethos_u_driver PUBLIC ${_NSX_ETHOSU_PUBLIC_DEFS})

# NOTE: `ETHOSU_TARGET_NPU_CONFIG` and `ETHOSU_TARGET_NPU` used to be defined
# here with a comment claiming upstream's config headers consumed them. They do
# not. `ETHOSU_TARGET_NPU_CONFIG` is a *CMake cache variable* read only by
# upstream's own CMakeLists.txt (which we deliberately do not
# add_subdirectory(); see the header comment), and `ETHOSU_TARGET_NPU` appears
# nowhere in the vendored tree at all. Verify with:
#     git grep -n ETHOSU_TARGET_NPU external/
# The gating that upstream source *does* perform is on ETHOSU_ARCH / ETHOSU55 /
# ETHOSU65 / ETHOSU85, all set above.
target_compile_definitions(nsx_ethos_u_driver PRIVATE
    ETHOSU_LOG_ENABLE=$<BOOL:${NSX_ETHOSU_LOG_ENABLE}>
)

# With logging off, LOG_ERR() in the vendored device backends expands to
# nothing and leaves their `dev` parameter unused; the Arm tree stays
# pristine, so silence just that warning for the driver TUs instead of
# patching upstream. (gcc/clang/armclang all accept the flag.)
if(NOT NSX_ETHOSU_LOG_ENABLE)
    target_compile_options(nsx_ethos_u_driver PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang,ARMClang>:-Wno-unused-parameter>
    )
endif()

target_include_directories(nsx_ethos_u_driver PUBLIC ${_NSX_ETHOSU_PUBLIC_INCS})

# Upstream's src/ headers (e.g. ethosu_config_u85.h) are reachable from the
# upstream TUs via relative include; they don't need to be on the public
# interface. We add the path PRIVATE for our own build.
target_include_directories(nsx_ethos_u_driver PRIVATE
    ${_NSX_ETHOSU_VENDOR_SRC}
)

target_link_libraries(nsx_ethos_u_driver PUBLIC ${_NSX_ETHOSU_DEPS})

# ----------------------------------------------------------------------------
# Install
# ----------------------------------------------------------------------------

install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/includes-api
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nsx/modules/nsx-ethos-u-driver
)
install(DIRECTORY ${_NSX_ETHOSU_VENDOR_INC}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nsx/modules/nsx-ethos-u-driver/ethos-u-core-driver
)
install(TARGETS nsx_ethos_u_driver EXPORT nsxTargets)

# The override objects ship next to the archive: an installed consumer that
# links nsx::ethos_u_driver must still receive them on its link line, or it
# silently gets upstream's weak malloc() semaphore and no-op cache maintenance
# back. These paths are what the $<INSTALL_INTERFACE:> sources above resolve to.
install(FILES $<TARGET_OBJECTS:nsx_ethos_u_overrides>
    DESTINATION ${_NSX_ETHOSU_OBJ_INSTALL_DIR}
)
