# SPDX-License-Identifier: MIT

# Adds one CTest test per TEST_CASE in source, named <prefix>.<case>. Extra arguments follow the case name.
function(genonline_eos_add_cases target prefix source)
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/${source}" lines REGEX "^TEST_CASE\\([A-Za-z0-9_]+\\)")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
    foreach(line IN LISTS lines)
        string(REGEX REPLACE "^TEST_CASE\\(([A-Za-z0-9_]+)\\).*$" "\\1" case "${line}")
        add_test(NAME ${prefix}.${case} COMMAND ${target} ${case} ${ARGN})
    endforeach()
endfunction()

add_executable(genonline_eos_core_tests core_tests.cpp test_harness.h)
target_link_libraries(genonline_eos_core_tests PRIVATE genonline_eos_core)
genonline_eos_add_cases(genonline_eos_core_tests core core_tests.cpp)

add_executable(genonline_eos_linked_account_tests linked_account_tests.cpp test_harness.h)
target_link_libraries(genonline_eos_linked_account_tests PRIVATE genonline_eos_core)
genonline_eos_add_cases(genonline_eos_linked_account_tests linked_account linked_account_tests.cpp)

add_executable(genonline_eos_transport_tests transport_tests.cpp test_harness.h)
target_link_libraries(genonline_eos_transport_tests PRIVATE genonline_eos_core)
genonline_eos_add_cases(genonline_eos_transport_tests transport transport_tests.cpp)

add_executable(genonline_eos_anticheat_tests anticheat_tests.cpp test_harness.h)
target_link_libraries(genonline_eos_anticheat_tests PRIVATE genonline_eos_core)
genonline_eos_add_cases(genonline_eos_anticheat_tests anticheat anticheat_tests.cpp)

# The DLL tests load the staged DLL by path, so EOSSDK-Win32-Shipping.dll resolves from the same folder as in the game.
set(GENONLINE_EOS_EXPECT_MISSING_CREDENTIALS 0)
foreach(field PRODUCT_ID SANDBOX_ID DEPLOYMENT_ID CLIENT_ID CLIENT_SECRET)
    if ("${GENONLINE_EOS_${field}}" STREQUAL "")
        set(GENONLINE_EOS_EXPECT_MISSING_CREDENTIALS 1)
    endif()
endforeach()

add_executable(genonline_eos_dll_tests dll_tests.cpp test_harness.h)
target_link_libraries(genonline_eos_dll_tests PRIVATE GenOnline::PluginApi)
target_compile_definitions(genonline_eos_dll_tests PRIVATE
    GENONLINE_EOS_EXPECTED_VERSION="${GENONLINE_EOS_VERSION}"
    GENONLINE_EOS_EXPECT_MISSING_CREDENTIALS=${GENONLINE_EOS_EXPECT_MISSING_CREDENTIALS})
add_dependencies(genonline_eos_dll_tests genonline_eos_package)
genonline_eos_add_cases(genonline_eos_dll_tests dll dll_tests.cpp "$<TARGET_FILE:genonline_eos>")

# Checks the staged folder and its manifest after the build. The DLL tests and this check need the package target.
add_test(NAME package.staged_folder_matches_the_manifest
    COMMAND "${CMAKE_COMMAND}"
        "-DPLUGIN_DIR=${GENONLINE_EOS_STAGE_DIR}"
        "-DDLL_DIR=$<TARGET_FILE_DIR:genonline_eos>"
        "-DPLUGIN_VERSION=${GENONLINE_EOS_VERSION}"
        -P "${PROJECT_SOURCE_DIR}/cmake/CheckStagedPlugin.cmake")
