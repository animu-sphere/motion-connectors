# SPDX-License-Identifier: Apache-2.0
#
# Top-level project for motion-connectors: workspace composition, and nothing
# else. Shared project and package policy is in cmake/*.cmake; each component's
# targets and link lines are in its own CMakeLists.txt.
#
# The workspace builds two ways, and both are kept working
# (docs/architecture/WORKSPACE.md §4):
#   * OpenStrata: `ost build` and `ost test` configure this file against the
#     activated runtime's toolchain.
#   * Plain CMake: configure this file with CMAKE_PREFIX_PATH pointing at an
#     OpenUSD 26.08 install, or use CMakePresets.json:
#       cmake -S . -B build -DCMAKE_PREFIX_PATH=<usd-install>
#       cmake --build build --config Release
#       ctest --test-dir build -C Release
#
# Each identity in WORKSPACE.md §1 arrives as a self-contained CMake project
# (so `ost` can build it standalone) that is also add_subdirectory()-able from
# here, and is named below in the dependency order of WORKSPACE.md §2.1.
#
# Every connector is optional (WORKSPACE.md §4): each has a
# MOTIONCONNECTORS_BUILD_<NAME> option, so a build that wants VMC configures no
# OpenXR, WebSocket or browser dependency. The v0.1.0 connectors default ON, so
# `ost build` and a bare configure build the whole product; a connector that
# brings a third-party dependency of its own is added OFF, in the change that
# creates it. The transport and the wire format depend on nothing and are
# always built; the core and the tracker layer link usd-motion-plugins'
# motionCore, and are built whenever any connector is.
cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/MotionConnectorsProject.cmake")

project(MotionConnectors
    VERSION ${MOTIONCONNECTORS_VERSION}
    DESCRIPTION "Live motion input, normalized into the shared motion contract"
    LANGUAGES CXX)

motionconnectors_project_baseline()
include(GNUInstallDirs)

option(MOTIONCONNECTORS_BUILD_TESTS "Build motion-connectors tests" ${PROJECT_IS_TOP_LEVEL})
option(MOTIONCONNECTORS_BUILD_VMC "Build the VMC connector" ON)
option(MOTIONCONNECTORS_BUILD_MOCOPI "Build the mocopi connector" ON)
option(MOTIONCONNECTORS_BUILD_VRCHAT_OSC "Build the VRChat OSC connector" ON)
option(MOTIONCONNECTORS_BUILD_TOOLS "Build the command-line tools" ${PROJECT_IS_TOP_LEVEL})

set(_connectors_any_connector OFF)
if(MOTIONCONNECTORS_BUILD_VMC OR MOTIONCONNECTORS_BUILD_MOCOPI
   OR MOTIONCONNECTORS_BUILD_VRCHAT_OSC)
    set(_connectors_any_connector ON)
endif()

if(MOTIONCONNECTORS_BUILD_TESTS)
    # At the top level, so tests registered by components are discoverable by
    # `ctest` from this build tree. A component configured standalone enables
    # its own (cmake/MotionConnectorsProject.cmake).
    enable_testing()
endif()

# Resolve OpenUSD once for every component that reaches it, and refuse any
# release but the one the ecosystem pins. No connector opens a stage, but every
# one links usd-motion-plugins' motionCore, which is built against this OpenUSD
# (DEPENDENCIES.md §1). It is resolved here rather than in the first component
# that needs it because pxrConfig.cmake's imported targets are directory-scoped:
# found inside libs/motionConnectorCore, they would be invisible to its
# siblings. A build of the transport and the wire format alone asks for no
# OpenUSD at all.
if(_connectors_any_connector)
    find_package(pxr REQUIRED CONFIG)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/MotionConnectorsOpenUsd.cmake")
endif()

# The interpreter the workspace tests run under, kept as
# MOTIONCONNECTORS_TEST_PYTHON. After find_package(pxr), which names the Python
# OpenUSD was built against (cmake/MotionConnectorsProject.cmake says why the
# name is our own).
if(MOTIONCONNECTORS_BUILD_TESTS)
    motionconnectors_find_test_python()
endif()

# The shared leaves. The transport and the wire format have an empty edge set
# and no option of their own.
add_subdirectory("libs/motionConnectorTransport")
add_subdirectory("libs/motionConnectorOsc")
# The core and the tracker layer link usd-motion-plugins' motionCore, which is
# not in this tree: its descriptor pins the published artifact per target and
# `ost` puts it on CMAKE_PREFIX_PATH before the build, so the find_package in
# each resolves against a materialized package rather than a sibling.
if(_connectors_any_connector)
    add_subdirectory("libs/motionConnectorCore")
    add_subdirectory("libs/motionConnectorTracking")
endif()

# The connectors, each with its recorder. A recorder is a workspace member of
# its own under tools/ (WORKSPACE.md §2.1) and is built with its connector
# when the tools are.
#
# VMC links three usd-motion-plugins packages and both leaves.
if(MOTIONCONNECTORS_BUILD_VMC)
    add_subdirectory("libs/motionConnectorVmc")
    if(MOTIONCONNECTORS_BUILD_TOOLS)
        add_subdirectory("tools/vmcRecord")
    endif()
endif()
# mocopi links three usd-motion-plugins packages and the transport leaf -- and
# not the wire format, because this protocol is not OSC.
if(MOTIONCONNECTORS_BUILD_MOCOPI)
    add_subdirectory("libs/motionConnectorMocopi")
    if(MOTIONCONNECTORS_BUILD_TOOLS)
        add_subdirectory("tools/mocopiRecord")
    endif()
endif()
# VRChat OSC is the connector that is not a pose source -- it stops at tracker
# observations -- so it links motionCore and the two leaves and no sampling or
# recording package; its recorder takes the tracker layer and motionRecording
# instead.
if(MOTIONCONNECTORS_BUILD_VRCHAT_OSC)
    add_subdirectory("libs/motionConnectorVrchatOsc")
    if(MOTIONCONNECTORS_BUILD_TOOLS)
        add_subdirectory("tools/vrchatOscRecord")
    endif()
endif()

# The shared-contract CLI. It links every v0.1.0 connector so `list` reports
# the capabilities the adapters actually declare rather than a second table,
# so it is built only when all of them are.
if(MOTIONCONNECTORS_BUILD_TOOLS)
    if(MOTIONCONNECTORS_BUILD_VMC AND MOTIONCONNECTORS_BUILD_MOCOPI
       AND MOTIONCONNECTORS_BUILD_VRCHAT_OSC)
        add_subdirectory("tools/motionConnect")
    else()
        message(STATUS "motion-connectors: motion_connect needs every connector; not built")
    endif()
endif()

# Workspace-level tests: the documentation, and the installed-consumer lane.
if(MOTIONCONNECTORS_BUILD_TESTS)
    add_subdirectory("tests")
endif()

# Not CMAKE_INSTALL_DOCDIR, which is share/doc/<PROJECT_NAME>: the installed
# name is the repository's, and the installed-consumer lane checks it there.
install(FILES
    LICENSE
    README.md
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/motion-connectors")
