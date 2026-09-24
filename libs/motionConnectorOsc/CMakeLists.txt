# SPDX-License-Identifier: Apache-2.0
#
# motionConnectorOsc — the OSC 1.0 wire format, and nothing that uses it.
#
# A plain static library: it registers nothing with OpenUSD and ships no
# plugInfo.json (WORKSPACE.md §1.1). Its edge set is **empty**, and emptier than
# `motionConnectorTransport`'s in the one way a reader would not predict: it
# links no platform primitive either. A transport needs a socket and a mutex; a
# decoder is handed a byte range and returns messages, so the only thing below
# that is not this library's own sources is an interpreter to run the boundary
# check with. `tests/check_boundaries.py` enforces exactly that reading, with an
# allowlist that names `motionConnectorOsc` and the CMake keywords and nothing
# else.
#
# Arrived from usd-vrm-plugins' `osc`, with its history (that repository's
# MIG-4; WORKSPACE.md §3). No address literal may appear in it, tests included
# (WORKSPACE.md §2.2).

cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MotionConnectorsProject.cmake")

project(motionConnectorOsc
    VERSION ${MOTIONCONNECTORS_VERSION}
    DESCRIPTION "OSC 1.0 packet decoding, shared by every OSC-speaking adapter"
    LANGUAGES CXX)

motionconnectors_component_project()

add_library(motionConnectorOsc STATIC src/OscPacket.cpp)
add_library(motionConnectorOsc::motionConnectorOsc ALIAS motionConnectorOsc)
set_target_properties(motionConnectorOsc PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(motionConnectorOsc PUBLIC cxx_std_20)
target_compile_definitions(motionConnectorOsc PUBLIC MOTIONCONNECTOROSC_STATIC)
target_include_directories(motionConnectorOsc
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")

# No target_link_libraries at all, and the absence is the measurement. The
# transport leaf beside this one links `ws2_32` or `Threads::Threads`; a decoder
# links neither, so the boundary check's allowlist has no platform entry to make
# an exception for.

if(MSVC)
    # PUBLIC for the same reason the connectors use it: the installed headers
    # carry UTF-8 section references, and a consumer on a non-UTF-8 code page
    # warns C4819 without the flag.
    target_compile_options(motionConnectorOsc PUBLIC /utf-8)
endif()

motionconnectors_install_package(TARGET motionConnectorOsc)

if(MOTIONCONNECTOROSC_BUILD_TESTS)
    add_subdirectory(tests)
endif()
