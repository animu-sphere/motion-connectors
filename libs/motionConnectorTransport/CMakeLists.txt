# SPDX-License-Identifier: Apache-2.0
#
# motionConnectorTransport — the shared leaf every connector receives through.
#
# A plain static library: it registers nothing with OpenUSD and ships no
# plugInfo.json (WORKSPACE.md §1.1). Its edge set is **empty** — not short,
# none — and that is a measurement rather than an intention: the six files it
# was extracted from in usd-vrm-plugins included their own headers and the
# standard library and nothing else. No `find_package` below names a library of
# this repository or of usd-motion-plugins, and the first one that does is a
# contract change rather than a commit (WORKSPACE.md §2). The two that are here
# ask for the platform's threading primitive and for an interpreter to run the
# boundary check with, neither of which is a dependency direction — and
# `tests/check_boundaries.py` enforces exactly that reading, refusing every
# other package by name.
#
# Arrived from usd-vrm-plugins' `liveTransport`, with its history (that
# repository's MIG-4; WORKSPACE.md §3).

cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MotionConnectorsProject.cmake")

project(motionConnectorTransport
    VERSION ${MOTIONCONNECTORS_VERSION}
    DESCRIPTION "Shared UDP receiver, packet-capture format and diagnostic vehicle for live adapters"
    LANGUAGES CXX)

motionconnectors_component_project()

add_library(motionConnectorTransport STATIC
    src/Diagnostics.cpp
    src/PacketCapture.cpp
    src/UdpReceiver.cpp)
add_library(motionConnectorTransport::motionConnectorTransport ALIAS motionConnectorTransport)
set_target_properties(motionConnectorTransport PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(motionConnectorTransport PUBLIC cxx_std_20)
target_compile_definitions(motionConnectorTransport PUBLIC MOTIONCONNECTORTRANSPORT_STATIC)
target_include_directories(motionConnectorTransport
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        # src/ is on the private include path for PollTimeout.h, which is this
        # library's one internal header and the reason usd-vrm-plugins' OSC-2
        # could pay a debt its OSC-1 could not: an adapter cannot hold one
        # without diverging from its sibling, and a library can. It is
        # deliberately not installed.
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

# The platform's own transport and threading primitives, which are not a
# dependency direction: WORKSPACE.md §2 constrains which libraries of the
# ecosystem this leaf may reach, and its answer there is "none".
# `tests/check_boundaries.py` allows exactly these two names and nothing else,
# so the gate still fails on a library edge.
#
# Threads arrives with `DatagramQueue`, whose mutex is the whole of this
# library's answer to the threading question UdpReceiver.h states. libstdc++
# needs the link even for a std::mutex that is never contended.
if(WIN32)
    target_link_libraries(motionConnectorTransport PUBLIC ws2_32)
else()
    # Ask for `-pthread` rather than a bare `-lpthread`: on GCC the flag also
    # affects code generation, and a target that only gets the library links but
    # is not built for threads.
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(motionConnectorTransport PUBLIC Threads::Threads)
endif()

if(MSVC)
    # PUBLIC for the same reason the connectors use it: the installed headers
    # carry UTF-8 section references, and a consumer on a non-UTF-8 code page
    # warns C4819 without the flag.
    target_compile_options(motionConnectorTransport PUBLIC /utf-8)
endif()

motionconnectors_install_package(TARGET motionConnectorTransport)

if(MOTIONCONNECTORTRANSPORT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
