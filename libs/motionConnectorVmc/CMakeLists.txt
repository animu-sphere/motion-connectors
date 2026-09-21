# SPDX-License-Identifier: Apache-2.0
#
# motionConnectorVmc — the generic real-time input leaf.
#
# A plain static library, not a plugin bundle: it registers nothing with
# OpenUSD and ships no plugInfo.json (WORKSPACE.md §1). It links motionCore,
# motionSampling, motionRecording and motionConnectorTransport and nothing else; the CLI that may retarget
# and author a stage is a separate tool under tools/.

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION"
         _vmc_version LIMIT_COUNT 1)
    string(STRIP "${_vmc_version}" _vmc_version)
else()
    set(_vmc_version "0.1.0")
endif()

project(motionConnectorVmc
    VERSION ${_vmc_version}
    DESCRIPTION "VMC Protocol input adapter for the canonical motion pipeline"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED MOTIONCONNECTORVMC_BUILD_TESTS)
    if(DEFINED MOTIONCONNECTORS_BUILD_TESTS)
        set(_vmc_tests_default "${MOTIONCONNECTORS_BUILD_TESTS}")
    else()
        set(_vmc_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(MOTIONCONNECTORVMC_BUILD_TESTS "Build motionConnectorVmc tests"
           ${_vmc_tests_default})
endif()

# OpenUSD arrives only through motionCore's Gf value types. The pin travels with
# the find_package call rather than with the root project, because a standalone
# configure of this directory never composes the root (see cmake/UsdVrmOpenUsd.cmake).
if(NOT pxr_FOUND)
    find_package(pxr REQUIRED CONFIG)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/MotionConnectorsOpenUsd.cmake")

# In the composed workspace build both are already in-tree targets; a standalone
# configure resolves their installed packages.
if(NOT TARGET motionCore::motionCore)
    find_package(motionCore REQUIRED CONFIG)
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
# The OSC wire format, which this adapter held alone until a second consumer
# proved the surface neutral (WORKSPACE.md §2, osc-and-vrchat-trackers.md §3.1).
# A sibling of `motionConnectorTransport` rather than a layer of it: §2 forbids an edge
# between those two in both directions, so this adapter is the place they meet.
if(NOT TARGET motionConnectorOsc::motionConnectorOsc)
    find_package(motionConnectorOsc REQUIRED CONFIG)
endif()

# PacketCapture.cpp is gone rather than shrunk: the format's reader and writer
# are motionConnectorTransport's now, and what this adapter contributes to them is one
# string in a header. Diagnostics.cpp, UdpReceiver.cpp and OscPacket.cpp are
# what is left of three files that moved -- a code table, an event-to-code map,
# and a refusal-to-code map, which are the three things a shared library may not
# hold.
add_library(motionConnectorVmc STATIC
    src/Diagnostics.cpp
    src/FrameAssembler.cpp
    src/LiveSource.cpp
    src/OscPacket.cpp
    src/SkeletonMap.cpp
    src/UdpReceiver.cpp
    src/VmcMessage.cpp)
add_library(motionConnectorVmc::motionConnectorVmc ALIAS motionConnectorVmc)
set_target_properties(motionConnectorVmc PROPERTIES
    EXPORT_NAME motionConnectorVmc
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(motionConnectorVmc PUBLIC cxx_std_17)
target_compile_definitions(motionConnectorVmc PUBLIC MOTIONCONNECTORVMC_STATIC)
target_include_directories(motionConnectorVmc
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
target_link_libraries(motionConnectorVmc
    PUBLIC
        motionCore::motionCore
        motionSampling::motionSampling
        motionRecording::motionRecording
        motionConnectorTransport::motionConnectorTransport
        motionConnectorOsc::motionConnectorOsc)

# No `ws2_32` and no `Threads::Threads` here any more, and their absence is the
# clearest single measurement of what OSC-2 moved. Motion policy §8.2 still puts
# the socket inside the adapter *layer*; this adapter no longer opens one itself,
# so the platform's transport and threading primitives travel with
# motionConnectorTransport's exported target instead. `tests/check_boundaries.py` allows
# them by name either way, so a future direct link still has to be argued for.

if(MSVC)
    # PUBLIC for the same reason every leaf here uses it: the installed headers
    # carry UTF-8 section references, and a consumer on a non-UTF-8 code page
    # warns C4819 without the flag.
    target_compile_options(motionConnectorVmc PUBLIC /utf-8)
endif()

include(GNUInstallDirs)
install(TARGETS motionConnectorVmc
    EXPORT motionConnectorVmcTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/motionConnectorVmcConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/motionConnectorVmcConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionConnectorVmc")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/motionConnectorVmcConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(EXPORT motionConnectorVmcTargets
    FILE motionConnectorVmcTargets.cmake
    NAMESPACE motionConnectorVmc::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionConnectorVmc")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/motionConnectorVmcConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/motionConnectorVmcConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionConnectorVmc")

if(MOTIONCONNECTORVMC_BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    add_subdirectory(tests)
endif()

# The adapter's CLI, which ships in the same artifact as the library
# (WORKSPACE.md §5). Added after the library and its tests: the tool links the
# target above, and adding it before would name a target that does not exist
# yet. It is where WORKSPACE.md §2's adapter-tool permissions begin -- a tool
# may drive `vrmRetarget` and author a stage where the library may not -- which
# is also why `tests/check_boundaries.py` scans include/ and src/ only.
#
# The option is VRC-6's, and it is here for symmetry rather than for a need of
# this adapter's: `scripts/check_package_consumer.py` turns every package's CLI
# off while installing that package, because a consumer's prefix holds exactly
# the declared closure and a CLI's edges are the tool's rather than the
# library's. An option only one of three adapters defined would be a rule with
# an invisible exception.
#
# The `EXISTS` guard it replaces was written when this tool did not exist. Once
# it does, that test can never be false in a correct tree, so its only
# reachable effect was to mask a rename or a bad checkout -- the sibling's own
# comment says so at length, and this is that argument applied here.
# The recorder is a workspace member of its own here (tools/vmcRecord), not a
# subdirectory of its connector: WORKSPACE.md §2.1 puts every tool at the
# root, beside motion_connect. The root CMakeLists adds it.
