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
# supplied; the stubs emit real inline-asm intrinsics when __ARM_ARCH is set.

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
    set_source_files_properties(${_nsx_own_sources}
        TARGET_DIRECTORY nsx_ethos_u_driver
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
endif()

# A link-time smoke check: pulling ethosu_init() out of the archive drags in
# the bulk of the driver and proves there are no undefined references beyond
# the ones an application is expected to supply.
#
# Skipped when cross-compiling: a bare-metal arm-none-eabi link needs startup
# code, a linker script and syscall stubs that this harness deliberately does
# not carry. The static library itself still builds in that mode.
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(nsx_ethos_u_link_smoke link_smoke.c)
    target_link_libraries(nsx_ethos_u_link_smoke PRIVATE nsx::ethos_u_driver)
    target_include_directories(nsx_ethos_u_link_smoke PRIVATE "${NSX_ETHOSU_SMOKE_STUBS}")
    add_test(NAME link_smoke COMMAND nsx_ethos_u_link_smoke)

    add_subdirectory(unit)
endif()
