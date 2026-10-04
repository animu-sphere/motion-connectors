# SPDX-License-Identifier: Apache-2.0
#
# motionConnectorWire -- the `MotionFrame` wire format: encode and decode
# `openstrata.motion.frame/v1` (docs/design/FRAME_WIRE_FORMAT.md), and nothing
# that sends or receives one.
#
# A plain static library: it registers nothing with OpenUSD and ships no
# plugInfo.json (WORKSPACE.md §1.1). Its edge set is WORKSPACE §2.1's line,
# `motionConnectorCore` and, through it, `motionCore`: the codec spells the
# shared `MotionFrame`, and the pose inside it is `motionCore`'s. It links no
# transport -- a wire format needs no socket, and §2.2 forbids the edge in
# either direction -- and no JSON package: the strict reader it needs is in
# src/Json.cpp (DEPENDENCIES.md §4).

cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MotionConnectorsProject.cmake")

project(motionConnectorWire
    VERSION ${MOTIONCONNECTORS_VERSION}
    DESCRIPTION "The MotionFrame wire format, openstrata.motion.frame/v1"
    LANGUAGES CXX)

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

add_library(motionConnectorWire STATIC
    src/FrameWire.cpp
    src/Json.cpp)
add_library(motionConnectorWire::motionConnectorWire ALIAS motionConnectorWire)
set_target_properties(motionConnectorWire PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(motionConnectorWire PUBLIC cxx_std_20)
target_compile_definitions(motionConnectorWire PUBLIC MOTIONCONNECTORWIRE_STATIC)
target_include_directories(motionConnectorWire
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")

# The whole edge set. `motionConnectorCore` carries `motionCore` and the Gf
# value types with it; `motionCore` is named as well because the codec reads
# the joint vocabulary itself.
target_link_libraries(motionConnectorWire
    PUBLIC motionConnectorCore::motionConnectorCore motionCore::motionCore)

if(MSVC)
    # PUBLIC for the same reason every library here uses it: the installed
    # headers carry UTF-8 section references, and a consumer on a non-UTF-8
    # code page warns C4819 without the flag.
    target_compile_options(motionConnectorWire PUBLIC /utf-8)
endif()

motionconnectors_install_package(TARGET motionConnectorWire)

if(MOTIONCONNECTORWIRE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
