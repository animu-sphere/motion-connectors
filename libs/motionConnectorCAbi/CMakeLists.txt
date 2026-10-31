# SPDX-License-Identifier: Apache-2.0
#
# motionConnectorCAbi -- the in-process acquisition C ABI over the connectors
# (docs/design/C_ABI.md).
#
# The one shared library here, because a language binding loads it by name and
# one process must not get two copies of its handles. The connectors link
# PRIVATE and static: their C++ types and symbols stop at this library, and only
# `motion_*` is exported. Its edges are WORKSPACE §2.1's line. It adds no
# acquisition logic, so a source this configure leaves out is simply not listed
# by motion_source_get_info.

cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MotionConnectorsProject.cmake")

project(motionConnectorCAbi
    VERSION ${MOTIONCONNECTORS_VERSION}
    DESCRIPTION "The in-process acquisition C ABI over the motion connectors"
    LANGUAGES C CXX)

motionconnectors_component_project()

if(NOT pxr_FOUND)
    find_package(pxr REQUIRED CONFIG)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MotionConnectorsOpenUsd.cmake")

if(NOT TARGET motionCore::motionCore)
    find_package(motionCore REQUIRED CONFIG)
endif()
if(NOT TARGET motionConnectorCore::motionConnectorCore)
    find_package(motionConnectorCore REQUIRED CONFIG)
endif()

# Standalone, every source is on, as the descriptor requires. In the workspace,
# the root's options decide.
option(MOTIONCONNECTORS_BUILD_VMC "Expose the VMC connector" ON)
option(MOTIONCONNECTORS_BUILD_MOCOPI "Expose the mocopi connector" ON)
option(MOTIONCONNECTORS_BUILD_VRCHAT_OSC "Expose the VRChat OSC connector" ON)
option(MOTIONCONNECTORS_BUILD_WEBSOCKET "Expose the WebSocket connector" ON)

add_library(motionConnectorCAbi SHARED src/MotionConnector.cpp)
add_library(motionConnectorCAbi::motionConnectorCAbi ALIAS motionConnectorCAbi)
set_target_properties(motionConnectorCAbi PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    VERSION ${PROJECT_VERSION}
    SOVERSION 1)

target_compile_features(motionConnectorCAbi PRIVATE cxx_std_20)
target_compile_definitions(motionConnectorCAbi PRIVATE MOTIONCONNECTORCABI_EXPORTS)
target_include_directories(motionConnectorCAbi
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")

target_link_libraries(motionConnectorCAbi
    PRIVATE
        motionConnectorCore::motionConnectorCore
        motionCore::motionCore)

foreach(_cabi_source
        "VMC;Vmc" "MOCOPI;Mocopi" "VRCHAT_OSC;VrchatOsc" "WEBSOCKET;WebSocket")
    list(GET _cabi_source 0 _cabi_option)
    list(GET _cabi_source 1 _cabi_name)
    if(MOTIONCONNECTORS_BUILD_${_cabi_option})
        if(NOT TARGET motionConnector${_cabi_name}::motionConnector${_cabi_name})
            find_package(motionConnector${_cabi_name} REQUIRED CONFIG)
        endif()
        target_link_libraries(motionConnectorCAbi
            PRIVATE motionConnector${_cabi_name}::motionConnector${_cabi_name})
        target_compile_definitions(motionConnectorCAbi
            PRIVATE MOTIONCONNECTORCABI_HAS_${_cabi_option})
    endif()
endforeach()

# Only `motion_*` leaves. On Windows dllexport already says so. A static
# connector's default-visibility symbols would otherwise be re-exported on the
# other two; motionConnectorCAbi_exports holds every platform to it.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(motionConnectorCAbi PRIVATE "LINKER:--exclude-libs,ALL")
elseif(APPLE)
    target_link_options(motionConnectorCAbi PRIVATE "LINKER:-exported_symbol,_motion_*")
endif()

if(MSVC)
    target_compile_options(motionConnectorCAbi PRIVATE /utf-8)
    target_compile_definitions(motionConnectorCAbi PRIVATE NOMINMAX)
endif()

motionconnectors_install_package(TARGET motionConnectorCAbi)

if(MOTIONCONNECTORCABI_BUILD_TESTS)
    add_subdirectory(tests)
endif()
