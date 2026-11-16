# SPDX-License-Identifier: MIT
# Imports the 32-bit EOS SDK as the target Eos::Sdk.

set(GENONLINE_EOS_SDK_DLL "${GENONLINE_EOS_SDK_DIR}/Bin/EOSSDK-Win32-Shipping.dll")
set(GENONLINE_EOS_SDK_LIB "${GENONLINE_EOS_SDK_DIR}/Lib/EOSSDK-Win32-Shipping.lib")

foreach(required "${GENONLINE_EOS_SDK_DIR}/Include/eos_sdk.h" "${GENONLINE_EOS_SDK_DLL}" "${GENONLINE_EOS_SDK_LIB}")
    if (NOT EXISTS "${required}")
        message(FATAL_ERROR "The EOS SDK is incomplete: ${required} is missing. Set GENONLINE_EOS_SDK_DIR to a folder with Include, Lib and Bin.")
    endif()
endforeach()

add_library(eos_sdk SHARED IMPORTED GLOBAL)
set_target_properties(eos_sdk PROPERTIES
    IMPORTED_LOCATION "${GENONLINE_EOS_SDK_DLL}"
    IMPORTED_IMPLIB "${GENONLINE_EOS_SDK_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${GENONLINE_EOS_SDK_DIR}/Include")
add_library(Eos::Sdk ALIAS eos_sdk)
