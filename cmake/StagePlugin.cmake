# SPDX-License-Identifier: MIT
# Completes the staged plugin folder before the manifest is written.
#
# cmake -DPLUGIN_DIR=<folder> -DEOS_DLL=<path of EOSSDK-Win32-Shipping.dll> -P StagePlugin.cmake
#
# The folder belongs to the build. Every file the manifest would wrongly list, such as a leftover from an earlier build
# or a stale signature, is removed, and the EOS SDK DLL is copied in.

cmake_minimum_required(VERSION 3.14)

foreach(required PLUGIN_DIR EOS_DLL)
    if (NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

set(kept genonline_eos.dll EOSSDK-Win32-Shipping.dll plugin.manifest)

file(GLOB entries LIST_DIRECTORIES true RELATIVE "${PLUGIN_DIR}" "${PLUGIN_DIR}/*")
foreach(entry IN LISTS entries)
    list(FIND kept "${entry}" index)
    if (index EQUAL -1)
        message(STATUS "Removing ${entry} from the staged plugin folder")
        file(REMOVE_RECURSE "${PLUGIN_DIR}/${entry}")
    endif()
endforeach()

execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${EOS_DLL}" "${PLUGIN_DIR}/EOSSDK-Win32-Shipping.dll" RESULT_VARIABLE copyResult)
if (NOT copyResult EQUAL 0)
    message(FATAL_ERROR "Copying ${EOS_DLL} into the staged plugin folder failed")
endif()
