# SPDX-License-Identifier: Apache-2.0
#
# motionConnectorMocopi — the native live input leaf for one capture product.
#
# A plain static library, not a plugin bundle: it registers nothing with
# OpenUSD and ships no plugInfo.json (WORKSPACE.md §1). It links motionCore,
# motionConnectorCore, motionSampling, motionRecording and
# motionConnectorTransport and nothing else; the CLI that may author a stage is
# a separate tool under the root
# tools/.

cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MotionConnectorsProject.cmake")

project(motionConnectorMocopi
    VERSION ${MOTIONCONNECTORS_VERSION}
    DESCRIPTION "Native live capture input adapter for the canonical motion pipeline"
    LANGUAGES CXX)

motionconnectors_component_project()

# OpenUSD arrives only through motionCore's Gf value types. The pin travels with
# the find_package call rather than with the root project, because a standalone
# configure of this directory never composes the root (see
# cmake/MotionConnectorsOpenUsd.cmake).
if(NOT pxr_FOUND)
    find_package(pxr REQUIRED CONFIG)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MotionConnectorsOpenUsd.cmake")

# In the composed workspace build both are already in-tree targets; a standalone
# configure resolves their installed packages.
if(NOT TARGET motionCore::motionCore)
    find_package(motionCore REQUIRED CONFIG)
endif()
if(NOT TARGET motionConnectorCore::motionConnectorCore)
    find_package(motionConnectorCore REQUIRED CONFIG)
endif()
# What was one `motionRuntime` in usd-vrm-plugins is two packages here: the
# frame assembler reads `motionSampling`'s source interface and the live source
# writes `motionRecording`'s capture trace, so both are named rather than one
# standing in for the other (usd-motion-plugins WORKSPACE.md §1.1).
if(NOT TARGET motionSampling::motionSampling)
    find_package(motionSampling REQUIRED CONFIG)
endif()
if(NOT TARGET motionRecording::motionRecording)
    find_package(motionRecording REQUIRED CONFIG)
endif()
# The live half's shared leaf: the socket, the packet-capture format and the
# diagnostic vehicle, which this adapter and its sibling had written twice
# (WORKSPACE.md §2, osc-and-vrchat-trackers.md §3.2). Not a sibling edge and not
# a route to one -- it is a leaf under libs/ with an empty edge set of its own,
# and it is outside the aggregate product for the same reason an adapter is.
if(NOT TARGET motionConnectorTransport::motionConnectorTransport)
    find_package(motionConnectorTransport REQUIRED CONFIG)
endif()

# PacketCapture.cpp is gone rather than shrunk: the format's reader and writer
# are motionConnectorTransport's now, and what this adapter contributes to them is one
# string in a header. Diagnostics.cpp and UdpReceiver.cpp are what is left of
# two files that moved -- a code table and an event-to-code map, which are the
# two things a shared library may not hold.
add_library(motionConnectorMocopi STATIC
    src/Connector.cpp
    src/Diagnostics.cpp
    src/FrameAssembler.cpp
    src/LiveSource.cpp
    src/MotionPacket.cpp
    src/PacketChunk.cpp
    src/SkeletonMap.cpp
    src/UdpReceiver.cpp)
add_library(motionConnectorMocopi::motionConnectorMocopi ALIAS motionConnectorMocopi)
set_target_properties(motionConnectorMocopi PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(motionConnectorMocopi PUBLIC cxx_std_20)
target_compile_definitions(motionConnectorMocopi PUBLIC MOTIONCONNECTORMOCOPI_STATIC)
target_include_directories(motionConnectorMocopi
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
# The five edges WORKSPACE.md §2 permits.
target_link_libraries(motionConnectorMocopi
    PUBLIC
        motionConnectorCore::motionConnectorCore
        motionCore::motionCore
        motionSampling::motionSampling
        motionRecording::motionRecording
        motionConnectorTransport::motionConnectorTransport)

# No `ws2_32` here any more, and its absence is the clearest single measurement
# of what OSC-2 moved. Motion policy §8.2 still puts the socket inside the
# adapter *layer*; this adapter no longer opens one itself, so the platform's
# transport arrives through motionConnectorTransport's exported target instead.
# `tests/check_boundaries.py` keeps allowing it by name, because the permission
# is the contract's and not that file's to withdraw.
#
# `Threads::Threads` now arrives the same way, and that is a change in what this
# adapter *reserves* rather than in what it uses. The previous revision of this
# comment declined to ask for it because `DatagramQueue` is the sibling's and no
# source file here holds a mutex; that is still true, and the queue is still
# opt-in, but a shared library cannot offer one class its threading primitive and
# not another. Nothing here constructs a queue, so nothing here pays for one.

if(MSVC)
    # PUBLIC for the same reason every leaf here uses it: the installed headers
    # carry UTF-8 section references, and a consumer on a non-UTF-8 code page
    # warns C4819 without the flag.
    target_compile_options(motionConnectorMocopi PUBLIC /utf-8)
endif()

motionconnectors_install_package(TARGET motionConnectorMocopi)
install(DIRECTORY profiles/ DESTINATION "${CMAKE_INSTALL_DATADIR}/motion-connectors/profiles")

if(MOTIONCONNECTORMOCOPI_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# The adapter's CLI, which ships in the same artifact as the library
# (WORKSPACE.md §5). It is where WORKSPACE.md §2's adapter-tool permissions
# begin -- a tool may author a stage where the library may not -- which is also
# why `tests/check_boundaries.py` scans include/ and src/ only.
#
# The recorder is a workspace member of its own here (tools/mocopiRecord), not
# a subdirectory of its connector: WORKSPACE.md §2.1 puts every tool at the
# root, beside motion_connect. The root CMakeLists adds it, and the
# MOTIONCONNECTORMOCOPI_BUILD_TOOL option that guarded the subdirectory call is
# gone with it -- what that option existed for, installing a library without
# the CLI over it, is the installed-consumer lane's business here, and a member
# of the workspace is not this project's to switch off.
