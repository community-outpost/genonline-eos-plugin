# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.19)

# The plugin links the CRT statically so the game needs no extra runtime for it.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(genonline_eos LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The plugin builds only for Windows x86, the platform the game ships on, with MSVC or clang-cl.
if (NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4 OR NOT MSVC_CXX_ARCHITECTURE_ID STREQUAL "X86")
    message(FATAL_ERROR "genonline_eos builds only for Windows x86 with MSVC or clang-cl, but this is ${CMAKE_SYSTEM_NAME} "
        "with ${CMAKE_SIZEOF_VOID_P} byte pointers and the ${CMAKE_CXX_COMPILER_ID} compiler. Use the win32-msvc or "
        "win32-clang preset, or the x86 Visual Studio environment.")
endif()

set(GENONLINE_PLUGIN_SDK_DIR "${CMAKE_SOURCE_DIR}/../genonline-plugin-sdk" CACHE PATH "Folder of the GenOnline plugin SDK")
set(GENONLINE_EOS_SDK_DIR "${CMAKE_SOURCE_DIR}/EOS" CACHE PATH "Folder of the EOS SDK with Include, Lib and Bin")
set(GENONLINE_EOS_VERSION "2026.11.0" CACHE STRING "Plugin version of the form YYYY.MM.PATCH")

# The credentials come from the build environment and never from a tracked file.
set(GENONLINE_EOS_PRODUCT_ID "" CACHE STRING "EOS product id")
set(GENONLINE_EOS_SANDBOX_ID "" CACHE STRING "EOS sandbox id")
set(GENONLINE_EOS_DEPLOYMENT_ID "" CACHE STRING "EOS deployment id")
set(GENONLINE_EOS_CLIENT_ID "" CACHE STRING "EOS client id")
set(GENONLINE_EOS_CLIENT_SECRET "" CACHE STRING "EOS client secret")

foreach(field PRODUCT_ID SANDBOX_ID DEPLOYMENT_ID CLIENT_ID CLIENT_SECRET)
    if ("${GENONLINE_EOS_${field}}" MATCHES "[\"\\\\]")
        message(FATAL_ERROR "GENONLINE_EOS_${field} holds a quote or a backslash, which the generated header cannot carry")
    endif()
endforeach()

include(cmake/PluginVersion.cmake)
genonline_eos_check_version("${GENONLINE_EOS_VERSION}")

add_subdirectory("${GENONLINE_PLUGIN_SDK_DIR}" genonline-plugin-sdk)
include(cmake/EosSdk.cmake)

set(GENONLINE_EOS_STAGE_DIR "${CMAKE_BINARY_DIR}/plugins/genonline_eos")

add_compile_options(/W4 /permissive- /utf-8)

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
