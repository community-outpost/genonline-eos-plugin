# SPDX-License-Identifier: MIT

# Pure plugin logic without EOS. The plugin library and the unit tests both link it.
add_library(genonline_eos_core STATIC
    core/anticheat_core.cpp
    core/anticheat_core.h
    core/api_table.cpp
    core/api_table.h
    core/credentials.cpp
    core/credentials.h
    core/host_buffer.cpp
    core/host_buffer.h
    core/host_link.cpp
    core/host_link.h
    core/latency_estimator.cpp
    core/latency_estimator.h
    core/linked_account_machine.cpp
    core/linked_account_machine.h
    core/log_queue.cpp
    core/log_queue.h
    core/packet_queues.cpp
    core/packet_queues.h
    core/peer_registry.cpp
    core/peer_registry.h
    core/transport_core.cpp
    core/transport_core.h
    core/transport_rules.cpp
    core/transport_rules.h)
target_include_directories(genonline_eos_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(genonline_eos_core PUBLIC GenOnline::PluginApi)

# The one place with operating system calls. The plugin library and the tests both link it.
add_library(genonline_eos_platform STATIC
    platform/platform.cpp
    platform/platform.h)
target_include_directories(genonline_eos_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(genonline_eos_platform PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# Build settings and credentials go into a generated header in the build folder, never into a tracked file.
set(GENONLINE_EOS_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(plugin/build_config.h.in "${GENONLINE_EOS_GENERATED_DIR}/build_config.h" @ONLY)

add_library(genonline_eos SHARED
    plugin/anticheat_api.cpp
    plugin/eos_ids.cpp
    plugin/eos_ids.h
    plugin/eos_runtime.cpp
    plugin/eos_runtime.h
    plugin/exports.cpp
    plugin/linked_account_api.cpp
    plugin/plugin_state.cpp
    plugin/plugin_state.h
    plugin/runtime_module.h
    plugin/sub_tables.h
    plugin/transport_api.cpp)
target_include_directories(genonline_eos PRIVATE "${GENONLINE_EOS_GENERATED_DIR}")
target_link_libraries(genonline_eos PRIVATE genonline_eos_core genonline_eos_platform Eos::Sdk)

# The library name equals the folder name. The staged folder holds only files the manifest lists, so the import library
# and the PDB go elsewhere.
set_target_properties(genonline_eos PROPERTIES
    OUTPUT_NAME genonline_eos
    RUNTIME_OUTPUT_DIRECTORY "${GENONLINE_EOS_STAGE_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${GENONLINE_EOS_STAGE_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb")
foreach(config RELEASE RELWITHDEBINFO MINSIZEREL DEBUG)
    set_target_properties(genonline_eos PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config} "${GENONLINE_EOS_STAGE_DIR}"
        LIBRARY_OUTPUT_DIRECTORY_${config} "${GENONLINE_EOS_STAGE_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/lib"
        PDB_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/pdb")
endforeach()

# Release builds keep a PDB outside the staged folder for crash reports.
target_compile_options(genonline_eos PRIVATE $<$<CONFIG:Release>:/Zi>)
target_link_options(genonline_eos PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)

# Runs on every build: removes stray files from the staged folder and copies the EOS library next to the plugin.
add_custom_target(genonline_eos_stage
    COMMAND "${CMAKE_COMMAND}"
        "-DPLUGIN_DIR=${GENONLINE_EOS_STAGE_DIR}"
        "-DPLUGIN_FILE=$<TARGET_FILE_NAME:genonline_eos>"
        "-DEOS_LIBRARY=${GENONLINE_EOS_SDK_RUNTIME}"
        -P "${PROJECT_SOURCE_DIR}/cmake/StagePlugin.cmake"
    VERBATIM)
add_dependencies(genonline_eos_stage genonline_eos)

genonline_plugin_manifest(genonline_eos_manifest
    FOLDER "${GENONLINE_EOS_STAGE_DIR}"
    NAME genonline_eos
    VERSION "${GENONLINE_EOS_VERSION}"
    DEPENDS genonline_eos genonline_eos_stage)

# Writes the manifest on every build so it always matches the staged files.
add_custom_target(genonline_eos_package ALL)
add_dependencies(genonline_eos_package genonline_eos_manifest)
