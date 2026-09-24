# SPDX-License-Identifier: Apache-2.0
#
# vrchat_osc_record — the VRChat OSC adapter's CLI.
#
# A workspace member of its own, added from the repository root: WORKSPACE.md
# §2.1 puts every tool under the root tools/, beside motion_connect. It still
# ships *with* its connector -- §5 names one artifact carrying the library
# together with its CLI -- and a standalone configure of this directory
# resolves that library, the tracker layer and the recording package as
# installed packages, which is the configuration the artifact would be built
# from.

cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MotionConnectorsProject.cmake")

project(vrchatOscRecord
    VERSION ${MOTIONCONNECTORS_VERSION}
    DESCRIPTION "VRChat OSC session recorder"
    LANGUAGES CXX)

motionconnectors_component_project()

# A standalone configure of this directory resolves each as an installed
# package; the composed build already has the in-tree targets.
if(NOT TARGET motionConnectorVrchatOsc::motionConnectorVrchatOsc)
    find_package(motionConnectorVrchatOsc REQUIRED CONFIG)
endif()
if(NOT TARGET motionConnectorTracking::motionConnectorTracking)
    find_package(motionConnectorTracking REQUIRED CONFIG)
endif()
# `WriteCaptureTraceFile` and nothing else: the hand-off to a consumer is a
# `motion-capture-trace` file. That half of usd-vrm-plugins' motionRuntime is
# motionRecording here.
if(NOT TARGET motionRecording::motionRecording)
    find_package(motionRecording REQUIRED CONFIG)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MotionConnectorsUtf8CodePage.cmake")

add_executable(vrchat_osc_record
    src/Options.cpp
    src/SessionReport.cpp
    src/TraceExport.cpp
    src/main.cpp)
motionconnectors_use_utf8_code_page(vrchat_osc_record)

target_compile_features(vrchat_osc_record PRIVATE cxx_std_20)
# Three edges, and the second of them is the one WORKSPACE.md §2 wrote a
# paragraph for. `motionConnectorTracking` is granted to an adapter's *tool* and refused
# to its library, because which tracker is on which body region is an operator's
# statement about a rig — an adapter that resolved it would have invented a
# calibration and hidden it inside a decoder. VRC-6 is the first taker of that
# permission; until this line existed it was a rule with nothing to hold.
#
# `motionRuntime` arrives for `WriteCaptureTraceFile` alone, which is the
# hand-off to the product: a `motion-capture-trace` file, which `motion_capture`
# replays knowing nothing about this adapter (§2).
#
# §2 would permit this target to link `vrmRetarget` and OpenUSD's stage
# libraries the way `motion_retarget` does. It does not need them — the trace is
# the hand-off, and a stage is the product's to author — and a link line is the
# cheapest place to keep that true.
target_link_libraries(vrchat_osc_record
    PRIVATE
        motionConnectorVrchatOsc::motionConnectorVrchatOsc
        motionConnectorTracking::motionConnectorTracking
        motionRecording::motionRecording)

if(MSVC)
    target_compile_options(vrchat_osc_record PRIVATE /utf-8)
    # The pair every target here sets, and since VRC-6 it is a present need
    # rather than the insurance this comment used to describe: motionCore and
    # motionRuntime call exported Gf entry points, so windows.h's min/max macros
    # now have OpenUSD's own numeric_limits<>::max() uses to break.
    target_compile_definitions(vrchat_osc_record PRIVATE NOMINMAX _USE_MATH_DEFINES)
endif()

install(TARGETS vrchat_osc_record RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(VRCHATOSCRECORD_BUILD_TESTS)
    add_subdirectory(tests)
endif()
