# SPDX-FileCopyrightText: Copyright 2026 Ambiq Micro, Inc.
# SPDX-License-Identifier: Apache-2.0
#
# Archive-link acceptance check: prove the NSX strong overrides actually made
# it into a FINAL LINK, rather than losing to the weak definitions upstream
# ships inside ethosu_driver.c / ethosu_device_uXX.c.
#
# This is the regression test for the bug where the override TUs were plain
# members of libnsx_ethos_u_driver.a: the linker never extracted them (nothing
# was left undefined once ethosu_driver.c.obj came in with its weak
# definitions), so the module silently ran on upstream's malloc()-based
# semaphore and no-op cache maintenance. Nothing about that link failed.
#
# Two independent signals, both read straight out of the symbol table:
#
#   1. Binding. A symbol that resolved to our definition is GLOBAL ('T'); one
#      that resolved to upstream's is WEAK ('W'/'V'). This works no matter what
#      the function bodies look like, which matters because on the smoke stubs
#      SCB_CleanDCache() inlines away to nothing.
#   2. Presence of the pool storage itself (nsx_ethos_u_sem_pool). If that
#      symbol is in the image, src/nsx_ethos_u_semaphore.c reached the link.
#
# Deliberately NOT checked here: "is `malloc` undefined?". Upstream's weak
# ethosu_semaphore_create() does call malloc(), but overriding a weak
# definition does not delete its body -- the dead code (and its relocation
# against malloc) stays in ethosu_driver.c.o absent --gc-sections, so the test
# reports a false failure even on a correct link. The behavioural equivalent
# lives in link_smoke.c instead: our create() exhausts a fixed pool and returns
# NULL, which a malloc() wrapper never would.
#
# Invoke with:
#   cmake -DNM=<nm> -DOBJECT_FILE=<exe-or-relocatable> -P check_overrides.cmake

foreach(_required NM OBJECT_FILE)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "check_overrides.cmake: -D${_required}=... is required")
    endif()
endforeach()

if(NOT EXISTS "${OBJECT_FILE}")
    message(FATAL_ERROR "check_overrides.cmake: '${OBJECT_FILE}' does not exist")
endif()

execute_process(
    COMMAND "${NM}" "${OBJECT_FILE}"
    OUTPUT_VARIABLE _nm_out
    ERROR_VARIABLE _nm_err
    RESULT_VARIABLE _nm_rc)
if(NOT _nm_rc EQUAL 0)
    message(FATAL_ERROR "check_overrides.cmake: '${NM} ${OBJECT_FILE}' failed: ${_nm_err}")
endif()

string(REPLACE "\n" ";" _nm_lines "${_nm_out}")

# Mach-O prefixes C identifiers with an underscore; ELF does not.
function(nsx_symbol_type OUT_VAR SYMBOL)
    set(${OUT_VAR} "" PARENT_SCOPE)
    foreach(_line IN LISTS _nm_lines)
        if(_line MATCHES "^[0-9a-fA-F]*[ \t]+([A-Za-z])[ \t]+_?${SYMBOL}$")
            set(${OUT_VAR} "${CMAKE_MATCH_1}" PARENT_SCOPE)
            return()
        endif()
        # Undefined symbols have no address column.
        if(_line MATCHES "^[ \t]*([UuWwVv])[ \t]+_?${SYMBOL}$")
            set(${OUT_VAR} "${CMAKE_MATCH_1}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
endfunction()

set(_failures "")

# Every one of these is defined weakly by the vendored Arm sources and strongly
# by src/. 'T' (or Mach-O's 'T') means our definition is the one in the image.
set(_must_be_strong
    ethosu_semaphore_create
    ethosu_semaphore_destroy
    ethosu_semaphore_take
    ethosu_semaphore_give
    ethosu_flush_dcache
    ethosu_invalidate_dcache
    ethosu_inference_begin
    ethosu_inference_end
)
# NOTE: ethosu_address_remap is deliberately absent -- src/nsx_ethos_u_remap.c
# defines it weakly on purpose so a BSP can still override it (see that file).

foreach(_sym IN LISTS _must_be_strong)
    nsx_symbol_type(_type "${_sym}")
    if(_type STREQUAL "")
        list(APPEND _failures "${_sym}: not present in the link at all")
    elseif(_type MATCHES "^[WV]$")
        list(APPEND _failures
            "${_sym}: resolved to a WEAK definition ('${_type}') -- upstream's default won; the src/ override object never reached the link line")
    elseif(NOT _type MATCHES "^[Tt]$")
        list(APPEND _failures "${_sym}: unexpected symbol type '${_type}' (wanted T)")
    endif()
endforeach()

# Direct evidence the pool TU is present. Local ('b'/'d') or global, either way.
nsx_symbol_type(_pool_type "nsx_ethos_u_sem_pool")
if(_pool_type STREQUAL "")
    list(APPEND _failures
        "nsx_ethos_u_sem_pool: the semaphore pool storage is not in the link")
endif()

if(_failures)
    string(REPLACE ";" "\n  - " _msg "${_failures}")
    message(FATAL_ERROR
        "override acceptance FAILED for ${OBJECT_FILE}:\n  - ${_msg}\n")
endif()

message(STATUS "override acceptance OK: ${OBJECT_FILE}")
