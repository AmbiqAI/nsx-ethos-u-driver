# SPDX-FileCopyrightText: Copyright 2026 Ambiq Micro, Inc.
# SPDX-License-Identifier: Apache-2.0
#
# Host unit tests. Only added when not cross-compiling -- these run.
#
# The TU under test is compiled directly into each executable rather than
# pulled from libnsx_ethos_u_driver.a: linking the archive would also drag in
# upstream's ethosu_driver.c, whose own weak ethosu_semaphore_* definitions we
# would then be testing archive-member selection against rather than the
# override itself.

find_package(Threads REQUIRED)

set(_nsx_sem_tu "${NSX_ETHOSU_ROOT}/src/nsx_ethos_u_semaphore.c")

# Two link configurations: with application-supplied timebase hooks (strong
# symbols beating the weak defaults) and without.
foreach(_variant hooks nohooks)
    set(_tgt "nsx_ethos_u_semaphore_test_${_variant}")

    add_executable(${_tgt} test_semaphore.c "${_nsx_sem_tu}")

    target_include_directories(${_tgt} PRIVATE
        "${NSX_ETHOSU_ROOT}/includes-api"
        "${NSX_ETHOSU_ROOT}/external/ethos-u-core-driver/include"
        "${NSX_ETHOSU_SMOKE_STUBS}")

    target_link_libraries(${_tgt} PRIVATE Threads::Threads)

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${_tgt} PRIVATE -Wall -Wextra -Werror)
    endif()

    # Opens the test-only seam in the TU (nsx_ethos_u_sem_test_set_count).
    target_compile_definitions(${_tgt} PRIVATE NSX_ETHOSU_SEM_TEST_ACCESS=1)

    if(_variant STREQUAL "hooks")
        target_compile_definitions(${_tgt} PRIVATE NSX_SEM_TEST_WITH_HOOKS=1)
    endif()

    add_test(NAME semaphore_${_variant} COMMAND ${_tgt})
    # Every wait in the suite is bounded by a giver thread or a deadline; if
    # one hangs, that is the bug and the timeout is how we see it.
    set_tests_properties(semaphore_${_variant} PROPERTIES TIMEOUT 60)
endforeach()

# PMU symbolic -> hardware event mapping (see test_pmu_event_map.c). Compiles
# the real upstream PMU TUs for the configured NPU family against a fake
# register block; no NSX code involved.
set(_nsx_pmu_vendor "${NSX_ETHOSU_ROOT}/external/ethos-u-core-driver")
# The module computes its family in its own directory scope; derive it again
# here from the same cache variable rather than reaching into that scope.
string(TOLOWER "${NSX_ETHOSU_NPU_CONFIG}" _nsx_pmu_cfg)
if(NOT _nsx_pmu_cfg MATCHES "^ethos-(u[0-9]+)-[0-9]+$")
    set(_nsx_pmu_cfg "ethos-u85-256")
    string(REGEX MATCH "^ethos-(u[0-9]+)-[0-9]+$" _ "${_nsx_pmu_cfg}")
endif()
set(_nsx_pmu_family "${CMAKE_MATCH_1}")
string(TOUPPER "ETHOS${_nsx_pmu_family}" _nsx_pmu_define)
add_executable(nsx_ethos_u_pmu_event_map_test
    test_pmu_event_map.c
    "${_nsx_pmu_vendor}/src/ethosu_pmu.c"
    "${_nsx_pmu_vendor}/src/ethosu_pmu_${_nsx_pmu_family}.c")
target_include_directories(nsx_ethos_u_pmu_event_map_test PRIVATE
    "${_nsx_pmu_vendor}/include"
    "${_nsx_pmu_vendor}/src"
    "${NSX_ETHOSU_SMOKE_STUBS}")
target_compile_definitions(nsx_ethos_u_pmu_event_map_test PRIVATE
    ETHOSU ETHOSU_ARCH=${_nsx_pmu_family} ${_nsx_pmu_define})
add_test(NAME pmu_event_map COMMAND nsx_ethos_u_pmu_event_map_test)
