# SPDX-FileCopyrightText: Copyright 2026 Ambiq Micro, Inc.
# SPDX-License-Identifier: Apache-2.0
#
# nsx-ethos-u-driver — NSX integration of Arm's ethos-u-core-driver.
#
# Layout:
#   external/ethos-u-core-driver/   - upstream driver, vendored as a git
#                                     submodule (Apache-2.0). We compile its
#                                     sources directly with NSX toolchain
#                                     flags rather than calling
#                                     add_subdirectory() on its CMakeLists.
#   includes-api/                   - public NSX-flavoured facade.
#   src/                            - weak-symbol overrides (cache, IRQ
#                                     trampoline, begin/end probes, default
#                                     identity address remap).
#
# Approach (B): explicit source listing.
#
#   Upstream's own CMakeLists.txt drives target-CPU selection via
#   `CMAKE_SYSTEM_PROCESSOR` (or `TARGET_CPU` when used in conjunction with
#   ethos-u-core-platform toolchain files). It also performs its own flag
#   manipulation and assumes a gcc-style driver. To remain aligned with the
#   NSX toolchain model (NSX_CPU, NSX_FPU, NSX_ABI_FLAGS, nsx_apply_toolchain_flags)
#   and to keep armclang / ATfE behaviour predictable, we deliberately do NOT
#   `add_subdirectory()` upstream. Instead we list its sources here and let
#   our board-supplied `${NSX_BOARD_FLAGS_TARGET}` carry the CPU/FPU flags.
#
#   The upstream config headers (`src/ethosu_config_uX5.h`) still need a
#   compile-time NPU family selector, which we provide via
#   `ETHOSU_TARGET_NPU_CONFIG`. The per-NPU device backend
#   (`ethosu_device_uXX.cc`) is selected from the family token below.
#
# If you ever need to re-evaluate this trade-off, the pivot point is whether
# upstream's CMakeLists begins to bake in flag generation we can't easily
# replicate. As of ethos-u-core-driver 25.x it does not.

if(NOT DEFINED NSX_BOARD_FLAGS_TARGET)
    message(FATAL_ERROR
        "Board must define NSX_BOARD_FLAGS_TARGET before nsx-ethos-u-driver is added.")
endif()
if(NOT DEFINED NSX_CPU)
    message(FATAL_ERROR
        "NSX_CPU must be defined before nsx-ethos-u-driver is added.")
endif()

# ----------------------------------------------------------------------------
# Configurables
# ----------------------------------------------------------------------------

set(NSX_ETHOSU_NPU_CONFIG "ethos-u85-256" CACHE STRING
    "Ethos-U target NPU config (e.g. ethos-u55-128, ethos-u65-256, ethos-u85-256)")

option(NSX_ETHOSU_BUILD_PMU
    "Build the Ethos-U PMU helper TU from upstream" ON)

# Parse the family (u55 / u65 / u85) from the config string. Used both to
# pick the device backend source and to set per-family compile defines that
# upstream's headers gate on.
string(REGEX MATCH "ethos-u([0-9]+)" _NSX_ETHOSU_FAMILY_MATCH "${NSX_ETHOSU_NPU_CONFIG}")
if(NOT _NSX_ETHOSU_FAMILY_MATCH)
    message(FATAL_ERROR
        "nsx-ethos-u-driver: NSX_ETHOSU_NPU_CONFIG='${NSX_ETHOSU_NPU_CONFIG}' "
        "is not of the form 'ethos-uNN-MACS'.")
endif()
set(NSX_ETHOSU_FAMILY_NUM "${CMAKE_MATCH_1}")
set(NSX_ETHOSU_FAMILY "u${NSX_ETHOSU_FAMILY_NUM}")

# ----------------------------------------------------------------------------
# Vendored upstream
# ----------------------------------------------------------------------------

set(_NSX_ETHOSU_VENDOR_DIR ${CMAKE_CURRENT_LIST_DIR}/external/ethos-u-core-driver)
set(_NSX_ETHOSU_VENDOR_INC ${_NSX_ETHOSU_VENDOR_DIR}/include)
set(_NSX_ETHOSU_VENDOR_SRC ${_NSX_ETHOSU_VENDOR_DIR}/src)

if(NOT EXISTS "${_NSX_ETHOSU_VENDOR_INC}/ethosu_driver.h")
    message(FATAL_ERROR
        "nsx-ethos-u-driver: vendored ethos-u-core-driver not found at "
        "${_NSX_ETHOSU_VENDOR_DIR}. Initialise the submodule:\n"
        "  git submodule update --init --recursive")
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
            "Did the submodule layout change?")
    endif()
endforeach()

# ----------------------------------------------------------------------------
# Library target
# ----------------------------------------------------------------------------

add_library(nsx_ethos_u_driver STATIC
    ${_NSX_ETHOSU_UPSTREAM_SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/src/nsx_ethos_u.c
    ${CMAKE_CURRENT_LIST_DIR}/src/nsx_ethos_u_cache.c
    ${CMAKE_CURRENT_LIST_DIR}/src/nsx_ethos_u_callbacks.c
    ${CMAKE_CURRENT_LIST_DIR}/src/nsx_ethos_u_remap.c
)
set_target_properties(nsx_ethos_u_driver PROPERTIES EXPORT_NAME ethos_u_driver)
add_library(nsx::ethos_u_driver ALIAS nsx_ethos_u_driver)

target_compile_definitions(nsx_ethos_u_driver PUBLIC
    NSX_HAS_NPU=1
    NSX_ETHOSU_NPU_FAMILY_${NSX_ETHOSU_FAMILY}=1
    # Upstream consumes this string (quoted) in its config header to gate
    # NPU-specific code paths. Keep it on the PUBLIC interface so apps that
    # include `ethosu_driver.h` directly see the same value.
    ETHOSU_TARGET_NPU_CONFIG="${NSX_ETHOSU_NPU_CONFIG}"
)

# `ETHOS_U_NPU_CONFIG_ID` is the form used by some upstream headers without
# the leading "ethos-" prefix; harmless to expose both.
target_compile_definitions(nsx_ethos_u_driver PRIVATE
    ETHOSU_TARGET_NPU=${NSX_ETHOSU_FAMILY_NUM}
)

target_include_directories(nsx_ethos_u_driver PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/includes-api>
    $<BUILD_INTERFACE:${_NSX_ETHOSU_VENDOR_INC}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/nsx/modules/nsx-ethos-u-driver/includes-api>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/nsx/modules/nsx-ethos-u-driver/ethos-u-core-driver/include>
)

# Upstream's src/ headers (e.g. ethosu_config_u85.h) are reachable from the
# upstream TUs via relative include; they don't need to be on the public
# interface. We add the path PRIVATE for our own build.
target_include_directories(nsx_ethos_u_driver PRIVATE
    ${_NSX_ETHOSU_VENDOR_SRC}
)

target_link_libraries(nsx_ethos_u_driver PUBLIC
    ${NSX_BOARD_FLAGS_TARGET}
    nsx::cmsis_core
    nsx_core
    nsx_soc_hal
)

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
