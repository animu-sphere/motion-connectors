# SPDX-License-Identifier: Apache-2.0
#
# motionConnectorVrchatOsc — the tracker input leaf.
#
# A plain static library, not a plugin bundle: it registers nothing with OpenUSD
# and ships no plugInfo.json (WORKSPACE.md §1). The CLI that may retarget and
# author a stage is a separate tool under tools/.
#
# ## Three link lines, and the one that is still not here
#
# `motionConnectorTransport`, `motionConnectorOsc` and `motionCore`.
# WORKSPACE.md §2 permits a connector the sampling and recording packages as
# well, and those are what a connector takes when it produces a *pose*; this
# one still produces none. What
# VRC-3 added is one layer below that: a tracker observation in the canonical
# basis, which is a value the rest of this repository can read and therefore a
# `GfVec3f` and a `GfQuatf` (usd-vrm-plugins' osc-and-vrchat-trackers.md §5, §9).
#
# **The edge is taken for the value types**, and no `motion::` type is named in
# this library yet. That is the contract's route rather than a convenience:
# §2 gives an adapter `motionCore` and does not give it `pxr`, so Gf arrives
# through the library whose contract already carries it, and the boundary check
# holds the allowlist that says which OpenUSD libraries may follow.
#
# **The closure grows here, exactly once.** VRC-0 recorded that this adapter's
# binaries imported no OpenUSD at all and predicted that a decoder producing a
# canonical value would change it; VRC-2 arrived and it did not, because the wire format
# links nothing. VRC-3 is the one that does: the suite that calls the conversion
# now loads usd_gf and its dependencies, which is why tests/ gained a PATH
# modification on Windows.
#
# What the boundary check inspects had to change with it, and the reason is in
# this line: a **static** library contributes only the objects a binary
# references, so an exe that calls none of `TrackingSpace.cpp` imports none of
# what that file links. tests/CMakeLists.txt carries the rule that came out of
# it.

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION"
         _vrchatosc_version LIMIT_COUNT 1)
    string(STRIP "${_vrchatosc_version}"
           _vrchatosc_version)
else()
    set(_vrchatosc_version "0.1.0")
endif()

project(motionConnectorVrchatOsc
    VERSION ${_vrchatosc_version}
    DESCRIPTION "VRChat OSC tracker input adapter for the canonical motion pipeline"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED MOTIONCONNECTORVRCHATOSC_BUILD_TESTS)
    if(DEFINED MOTIONCONNECTORS_BUILD_TESTS)
        set(_vrchatosc_tests_default "${MOTIONCONNECTORS_BUILD_TESTS}")
    else()
        set(_vrchatosc_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(MOTIONCONNECTORVRCHATOSC_BUILD_TESTS "Build motionConnectorVrchatOsc tests"
           ${_vrchatosc_tests_default})
endif()

# In the composed workspace build this is already an in-tree target; a
# standalone configure of this directory resolves its installed package.
if(NOT TARGET motionConnectorTransport::motionConnectorTransport)
    find_package(motionConnectorTransport REQUIRED CONFIG)
endif()
if(NOT TARGET motionConnectorOsc::motionConnectorOsc)
    find_package(motionConnectorOsc REQUIRED CONFIG)
endif()
# The value types, by the route WORKSPACE.md §2 gives an adapter: Gf arrives
# through motionCore and never through a direct `find_package(pxr)` here.
if(NOT TARGET motionCore::motionCore)
    find_package(motionCore REQUIRED CONFIG)
endif()

# Six files. Two of them exist because a shared library may not hold what is
# in them: a code table, and the map from a transport event to one of its rows
# (WORKSPACE.md §2's diagnostic split). The capture format contributes a third
# thing of the same kind and it is one string, so it lives in a header.
#
# `AddressInventory.cpp` is the first file here that is neither of those: it is
# this adapter reading a session rather than describing itself, and it is what
# made it a library — a decoder with one caller is a decoder shaped like that
# caller (usd-vrm-plugins' osc-and-vrchat-trackers.md §3.1). It carries the same diagnostic split
# as the other two, in its one refusal path.
# `TrackerMessage.cpp` is the fourth, and it is the first file here that gives a
# byte a meaning: an address becomes a tracker identity and a channel, and three
# floats become three floats this adapter is willing to name (VRC-2). It is
# still not a pose — position and rotation arrive on separate addresses and the
# window they belong to is VRC-4's — so the link line below is unchanged by it.
# `TrackingSpace.cpp` is the fifth and the first to produce a value outside this
# adapter's own vocabulary: the sender's axes into the canonical ones, measured
# from a labelled session rather than read off VRChat's documentation (VRC-3).
# It is the file the motionCore edge below exists for.
# `FrameAssembler.cpp` is the sixth and the first that *decides* rather than
# converts: which observations belong to one frame, which tracker has stopped
# reporting, and when a stream has become a different stream (VRC-4). It adds
# no edge -- the canonical value types arrived with VRC-3 and a frame is made
# of them -- and it names no body role, which is what keeps the assignment
# policy outside this library (usd-vrm-plugins' osc-and-vrchat-trackers.md §5.1).
add_library(motionConnectorVrchatOsc STATIC
    src/AddressInventory.cpp
    src/Diagnostics.cpp
    src/FrameAssembler.cpp
    src/TrackerMessage.cpp
    src/TrackingSpace.cpp
    src/UdpReceiver.cpp)
add_library(motionConnectorVrchatOsc::motionConnectorVrchatOsc ALIAS motionConnectorVrchatOsc)
set_target_properties(motionConnectorVrchatOsc PROPERTIES
    EXPORT_NAME motionConnectorVrchatOsc
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(motionConnectorVrchatOsc PUBLIC cxx_std_17)
target_compile_definitions(motionConnectorVrchatOsc PUBLIC MOTIONCONNECTORVRCHATOSC_STATIC)
target_include_directories(motionConnectorVrchatOsc
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
# The three edges this library has. The platform's socket and its threading
# primitive arrive behind the first, as they do for both siblings since OSC-2;
# nothing at all arrives behind the second; and OpenUSD's value-type layer
# arrives behind the third, which is the change VRC-3 makes to this binary's
# closure. All three are PUBLIC because the installed headers name types from
# each.
target_link_libraries(motionConnectorVrchatOsc
    PUBLIC
        motionConnectorTransport::motionConnectorTransport
        motionConnectorOsc::motionConnectorOsc
        motionCore::motionCore)

if(MSVC)
    # PUBLIC for the same reason the siblings use it: the installed headers
    # carry UTF-8 section references, and a consumer on a non-UTF-8 code page
    # warns C4819 without the flag.
    target_compile_options(motionConnectorVrchatOsc PUBLIC /utf-8)
endif()

include(GNUInstallDirs)
install(TARGETS motionConnectorVrchatOsc
    EXPORT motionConnectorVrchatOscTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/motionConnectorVrchatOscConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/motionConnectorVrchatOscConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionConnectorVrchatOsc")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/motionConnectorVrchatOscConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(EXPORT motionConnectorVrchatOscTargets
    FILE motionConnectorVrchatOscTargets.cmake
    NAMESPACE motionConnectorVrchatOsc::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionConnectorVrchatOsc")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/motionConnectorVrchatOscConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/motionConnectorVrchatOscConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionConnectorVrchatOsc")

if(MOTIONCONNECTORVRCHATOSC_BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    add_subdirectory(tests)
endif()

# The connector's CLI, which ships in the same artifact as the library
# (WORKSPACE.md §5) and is where §2's connector-tool permissions begin: the CLI
# links `motionConnectorTracking` and `motionRecording` where the library links
# neither, because assignment is an operator's statement about a rig and a
# decoder that resolved it would have invented a calibration.
#
# The recorder is a workspace member of its own here (tools/vrchatOscRecord),
# not a subdirectory of its connector: WORKSPACE.md §2.1 puts every tool at the
# root, beside motion_connect. The root CMakeLists adds it, and the
# MOTIONCONNECTORVRCHATOSC_BUILD_TOOL option that guarded the subdirectory call
# is gone with it -- what that option existed for, configuring this library
# without the two packages only its CLI needs, is what a member of its own
# already gives.
