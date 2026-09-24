# SPDX-License-Identifier: Apache-2.0
#
# motionConnectorTracking — which tracker is which body region, and nothing below or
# above that.
#
# A plain static library, not a plugin bundle: it registers nothing with OpenUSD
# and ships no plugInfo.json (WORKSPACE.md §1). Its allowed edge set was empty
# until VRC-5 and is now one line — `motionCore`, taken by the solve and by
# nothing else: a solve produces a `MotionPose`, which is that library's type.
#
# The two halves are held apart by the boundary check rather than by two CMake
# targets, and that is a deliberate choice. Splitting the assignment half into
# its own library would make the rule a link line and cost every consumer a
# second package, while the rule that actually matters — a `TrackerRegion` is
# never a `HumanJoint` — has no link line to fail on anyway (§2, which forbids
# the alias by name). So the check reads the sources, per file.
#
# Unlike the two leaves above it, this library is on the **product** side of
# §5's split, and nothing links it at all yet: §2's permission is
# `adapters/*/tools/* -> motionConnectorTracking` and no CLI has taken it (VRC-6). Nothing
# has to change for a product tool to link it either.

cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MotionConnectorsProject.cmake")

project(motionConnectorTracking
    VERSION ${MOTIONCONNECTORS_VERSION}
    DESCRIPTION "Tracker-to-body-region assignment, generic and outside every adapter"
    LANGUAGES CXX)

motionconnectors_component_project()

# The solve names OpenUSD's Gf value types and `motionCore`'s joint
# vocabulary; nothing here has a USD stage, Sdf, plug or file-format
# dependency, exactly as the package it links does not.
#
# `motionCore` is NOT an in-tree target here and never will be: it is
# usd-motion-plugins', pinned by digest in this library's descriptor and
# materialized by `ost library pull` before the build. The guard below is kept
# for the case the guard is for -- a consumer that already found it -- and the
# find_package is the normal path rather than the standalone one.
if(NOT pxr_FOUND)
    find_package(pxr REQUIRED CONFIG)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MotionConnectorsOpenUsd.cmake")

if(NOT TARGET motionCore::motionCore)
    find_package(motionCore REQUIRED CONFIG)
endif()

add_library(motionConnectorTracking STATIC
    src/TrackerAssignment.cpp
    src/TrackerObservation.cpp
    src/TrackerRegion.cpp
    src/TrackerSolve.cpp)
add_library(motionConnectorTracking::motionConnectorTracking ALIAS motionConnectorTracking)
set_target_properties(motionConnectorTracking PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(motionConnectorTracking PUBLIC cxx_std_20)
target_compile_definitions(motionConnectorTracking PUBLIC MOTIONCONNECTORTRACKING_STATIC)
target_include_directories(motionConnectorTracking
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")

# One link line, and it is the whole edge set. No platform primitive and no
# transport: this library opens nothing and waits for nothing, and what it
# gained at VRC-5 is a value type rather than an ability. `motionCore` carries
# the Gf target with it, so there is no second line for OpenUSD.
target_link_libraries(motionConnectorTracking PUBLIC motionCore::motionCore)

if(MSVC)
    # PUBLIC for the same reason every leaf here uses it: the installed headers
    # carry UTF-8 section references, and a consumer on a non-UTF-8 code page
    # warns C4819 without the flag.
    target_compile_options(motionConnectorTracking PUBLIC /utf-8)
endif()

motionconnectors_install_package(TARGET motionConnectorTracking)

if(MOTIONCONNECTORTRACKING_BUILD_TESTS)
    add_subdirectory(tests)
endif()
