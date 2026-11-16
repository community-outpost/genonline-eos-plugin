# SPDX-License-Identifier: MIT
# Checks that a plugin version has the form YYYY.MM.PATCH and is not below the lowest version the game accepts.

set(GENONLINE_EOS_LOWEST_VERSION "2026.11.0")

function(genonline_eos_check_version version)
    if (NOT version MATCHES "^([0-9][0-9][0-9][0-9])\\.([0-9][0-9]?)\\.([0-9]+)$")
        message(FATAL_ERROR "GENONLINE_EOS_VERSION '${version}' is not of the form YYYY.MM.PATCH")
    endif()
    if (CMAKE_MATCH_2 LESS 1 OR CMAKE_MATCH_2 GREATER 12)
        message(FATAL_ERROR "GENONLINE_EOS_VERSION '${version}' has no month between 1 and 12")
    endif()
    if (version VERSION_LESS GENONLINE_EOS_LOWEST_VERSION)
        message(FATAL_ERROR "GENONLINE_EOS_VERSION '${version}' is below ${GENONLINE_EOS_LOWEST_VERSION}, which the game refuses")
    endif()
endfunction()
