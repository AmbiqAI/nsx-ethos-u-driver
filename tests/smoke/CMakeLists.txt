# SPDX-FileCopyrightText: Copyright 2026 Ambiq Micro, Inc.
# SPDX-License-Identifier: Apache-2.0
#
# nsx-ethos-u-driver build-smoke harness.
#
# The repo-root CMakeLists.txt is a *module* fragment: it has no project()
# call and it hard-requires four NSX targets (nsx::soc_flags, nsx::cmsis_core,
# nsx::core, nsx::soc_hal) plus an `am_mcu_apollo.h` on the include path, all
# of which live in the NSX superbuild. That makes it impossible to configure
# the module standalone, so nothing in CI ever compiled a single line of it.
#
# This harness is the minimum scaffolding that makes the module configurable
# and buildable on its own:
#
#   * four stub INTERFACE targets standing in for the NSX dependencies, with
#     nsx::cmsis_core carrying stubs/ on its interface include path;
#   * stubs/cmsis_compiler.h and stubs/am_mcu_apollo.h (see those files).
#
# It is a COMPILE/LINK smoke test: it proves the module configures, that every
# TU (ours and the vendored Arm ones) compiles clean, and that the static
# archive links. It does not and cannot exercise NPU hardware.
#
# Usage (host compiler, no cross toolchain needed):
#
#   cmake -S tests/smoke -B build/smoke \
#         -DNSX_ETHOSU_NPU_CONFIG=ethos-u85-256 \
#         -DNSX_ETHOSU_LOG_ENABLE=OFF
#   cmake --build build/smoke
#   ctest --test-dir build/smoke --output-on-failure
#
# It also configures cleanly under arm-none-eabi-gcc if a toolchain file is
# supplied; the stubs emit real inline-asm intrinsics when the compiler reports
# an M-profile target (__ARM_ARCH_PROFILE == 'M') -- deliberately not plain
# __ARM_ARCH, which an aarch64 build host defines too.

cmake_minimum_required(VERSION 3.20)
project(nsx_ethos_u_smoke C)

include(GNUInstallDirs)
include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

get_filename_component(NSX_ETHOSU_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(NSX_ETHOSU_SMOKE_STUBS "${CMAKE_CURRENT_LIST_DIR}/stubs")

# ----------------------------------------------------------------------------
# Stub NSX dependency targets
# ----------------------------------------------------------------------------
#
# Real NSX builds attach CPU/FPU/ABI flags to nsx::soc_flags and the CMSIS
# headers to nsx::cmsis_core. Here they carry only what the module needs to
# compile: the stub include directory.

add_library(nsx_smoke_soc_flags INTERFACE)
add_library(nsx::soc_flags ALIAS nsx_smoke_soc_flags)

add_library(nsx_smoke_cmsis_core INTERFACE)
target_include_directories(nsx_smoke_cmsis_core INTERFACE "${NSX_ETHOSU_SMOKE_STUBS}")
add_library(nsx::cmsis_core ALIAS nsx_smoke_cmsis_core)

add_library(nsx_smoke_core INTERFACE)
add_library(nsx::core ALIAS nsx_smoke_core)

add_library(nsx_smoke_soc_hal INTERFACE)
target_include_directories(nsx_smoke_soc_hal INTERFACE "${NSX_ETHOSU_SMOKE_STUBS}")
add_library(nsx::soc_hal ALIAS nsx_smoke_soc_hal)

# ----------------------------------------------------------------------------
# The module under test
# ----------------------------------------------------------------------------

add_subdirectory("${NSX_ETHOSU_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/nsx-ethos-u-driver")

# Turn warnings up to -Werror, but only on the TUs *we* own. The vendored Arm
# sources stay on whatever the root CMakeLists configures for them -- holding
# upstream to our warning bar would create pressure to patch external/, which
# is provenance-protected.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    file(GLOB _nsx_own_sources CONFIGURE_DEPENDS "${NSX_ETHOSU_ROOT}/src/*.c")
    # The override TUs live in the nsx_ethos_u_overrides OBJECT library now, so
    # both targets have to be named or half of src/ would drop off -Werror.
    set_source_files_properties(${_nsx_own_sources}
        TARGET_DIRECTORY nsx_ethos_u_driver nsx_ethos_u_overrides
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
endif()

# ----------------------------------------------------------------------------
# Override acceptance: did the strong src/ definitions actually win the link?
# ----------------------------------------------------------------------------
#
# link_acceptance.c references only ethosu_init() and nsx_ethos_u_init() -- no
# hook addresses -- so it reproduces a plain application link exactly. The
# check itself lives in check_overrides.cmake and reads the symbol table.
#
# This is a *partial* (-r) link so it needs no startup code, linker script or
# syscall stubs, which means it runs in the cross build too -- the one that
# matters, since bare-metal arm-none-eabi is where the module ships.
add_executable(nsx_ethos_u_link_acceptance link_acceptance.c)
target_link_libraries(nsx_ethos_u_link_acceptance PRIVATE nsx::ethos_u_driver)
target_include_directories(nsx_ethos_u_link_acceptance PRIVATE "${NSX_ETHOSU_SMOKE_STUBS}")
target_link_options(nsx_ethos_u_link_acceptance PRIVATE -nostdlib -nostartfiles -Wl,-r)
set_target_properties(nsx_ethos_u_link_acceptance PROPERTIES
    # -r output is a relocatable object, not an image; skip any link check that
    # would expect an entry point.
    LINKER_LANGUAGE C)

find_program(NSX_ETHOSU_NM
    NAMES ${CMAKE_NM} "${CMAKE_C_COMPILER_TARGET}-nm" nm
    HINTS "${_NSX_ETHOSU_TOOLCHAIN_BIN}")
if(NSX_ETHOSU_NM)
    add_custom_command(TARGET nsx_ethos_u_link_acceptance POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DNM=${NSX_ETHOSU_NM}
            -DOBJECT_FILE=$<TARGET_FILE:nsx_ethos_u_link_acceptance>
            -P "${CMAKE_CURRENT_LIST_DIR}/check_overrides.cmake"
        VERBATIM
        COMMENT "Checking NSX overrides beat upstream's weak definitions")
else()
    message(WARNING
        "nsx-ethos-u-driver smoke: no nm found; skipping the override "
        "acceptance check.")
endif()

# ----------------------------------------------------------------------------
# Three-level acceptance: do the overrides survive a re-exporting middle layer?
# ----------------------------------------------------------------------------
#
#   nsx_ethos_u_link_transitive (exe)  ->  nsx_ethos_u_smoke_middle (STATIC)
#                                              ->  nsx::ethos_u_driver
#
# The application names ONLY the middle library. This is how the driver is
# actually consumed in the field (an app links a BSP/HAL wrapper, not the
# driver), and it is the one topology where the override mechanism can fail
# silently: the objects ride on the driver's INTERFACE_SOURCES, and a middle
# library that links the driver PRIVATE wraps it in $<LINK_ONLY:>, which
# propagates the *link* requirement but strips every usage requirement --
# sources included. The executable then links fine and quietly runs on
# upstream's weak malloc() semaphore and no-op cache maintenance.
#
# NSX_ETHOSU_SMOKE_MIDDLE_LINK exists to make that failure reproducible on
# demand rather than by hand-editing this file:
#
#   cmake -S tests/smoke -B build/smoke-private -DNSX_ETHOSU_SMOKE_MIDDLE_LINK=PRIVATE
#   cmake --build build/smoke-private --target nsx_ethos_u_link_transitive
#   # -> check_overrides.cmake fails: every ethosu_* hook resolved WEAK.
#
# PUBLIC is the default and the only value CI builds.
set(NSX_ETHOSU_SMOKE_MIDDLE_LINK "PUBLIC" CACHE STRING
    "Linkage the smoke middle library uses for nsx::ethos_u_driver (PUBLIC is \
correct; PRIVATE reproduces the silent override-loss failure)")
set_property(CACHE NSX_ETHOSU_SMOKE_MIDDLE_LINK PROPERTY STRINGS PUBLIC PRIVATE)
if(NOT NSX_ETHOSU_SMOKE_MIDDLE_LINK MATCHES "^(PUBLIC|PRIVATE)$")
    message(FATAL_ERROR
        "NSX_ETHOSU_SMOKE_MIDDLE_LINK must be PUBLIC or PRIVATE, got "
        "'${NSX_ETHOSU_SMOKE_MIDDLE_LINK}'.")
endif()

add_library(nsx_ethos_u_smoke_middle STATIC link_middle.c)
target_link_libraries(nsx_ethos_u_smoke_middle
    ${NSX_ETHOSU_SMOKE_MIDDLE_LINK} nsx::ethos_u_driver)
target_include_directories(nsx_ethos_u_smoke_middle
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}"
    PRIVATE "${NSX_ETHOSU_SMOKE_STUBS}")

add_executable(nsx_ethos_u_link_transitive link_transitive.c)
# Deliberately NOT nsx::ethos_u_driver: the middle library is the only thing
# named here, exactly as an application would.
target_link_libraries(nsx_ethos_u_link_transitive PRIVATE nsx_ethos_u_smoke_middle)
target_link_options(nsx_ethos_u_link_transitive PRIVATE -nostdlib -nostartfiles -Wl,-r)
set_target_properties(nsx_ethos_u_link_transitive PROPERTIES LINKER_LANGUAGE C)

if(NSX_ETHOSU_NM)
    add_custom_command(TARGET nsx_ethos_u_link_transitive POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DNM=${NSX_ETHOSU_NM}
            -DOBJECT_FILE=$<TARGET_FILE:nsx_ethos_u_link_transitive>
            -P "${CMAKE_CURRENT_LIST_DIR}/check_overrides.cmake"
        VERBATIM
        COMMENT "Checking NSX overrides survive a re-exporting middle library")

    # Same check again, as a ctest case. The POST_BUILD run is the hard gate
    # (it fails the cross build too, where ctest is never invoked); this one
    # exists so the topology shows up as a named result in the host test
    # report instead of being invisible when everything passes.
    add_test(NAME link_overrides_win_transitive COMMAND ${CMAKE_COMMAND}
        -DNM=${NSX_ETHOSU_NM}
        -DOBJECT_FILE=$<TARGET_FILE:nsx_ethos_u_link_transitive>
        -P "${CMAKE_CURRENT_LIST_DIR}/check_overrides.cmake")
endif()

# A link-time smoke check: pulling ethosu_init() out of the archive drags in
# the bulk of the driver and proves there are no undefined references beyond
# the ones an application is expected to supply. It then exercises the
# overrides through *behaviour* (see link_smoke.c) so a regression to
# upstream's weak defaults fails the run, not just the symbol table.
#
# Skipped when cross-compiling: a bare-metal arm-none-eabi link needs startup
# code, a linker script and syscall stubs that this harness deliberately does
# not carry. The static library and the acceptance check above still run in
# that mode.
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(nsx_ethos_u_link_smoke link_smoke.c)
    target_link_libraries(nsx_ethos_u_link_smoke PRIVATE nsx::ethos_u_driver)
    target_include_directories(nsx_ethos_u_link_smoke PRIVATE "${NSX_ETHOSU_SMOKE_STUBS}")
    add_test(NAME link_smoke COMMAND nsx_ethos_u_link_smoke)

    if(NSX_ETHOSU_NM)
        add_test(NAME link_overrides_win COMMAND ${CMAKE_COMMAND}
            -DNM=${NSX_ETHOSU_NM}
            -DOBJECT_FILE=$<TARGET_FILE:nsx_ethos_u_link_smoke>
            -P "${CMAKE_CURRENT_LIST_DIR}/check_overrides.cmake")
    endif()

    add_subdirectory(unit)
endif()
