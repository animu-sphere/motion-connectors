# SPDX-License-Identifier: Apache-2.0
#
# vmc_record — the VMC adapter's CLI.
#
# A workspace tool composed by the repository root. A standalone configure
# consumes the installed connector plus explicit downstream motion packages.

cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MotionConnectorsProject.cmake")

project(vmcRecord
    VERSION ${MOTIONCONNECTORS_VERSION}
    DESCRIPTION "VMC session recorder"
    LANGUAGES CXX)

motionconnectors_component_project()

# A standalone configure of this directory resolves the adapter as an installed
# package; the composed build already has the in-tree target.
if(NOT TARGET motionConnectorVmc::motionConnectorVmc)
    find_package(motionConnectorVmc REQUIRED CONFIG)
endif()

if(NOT TARGET motionRecording::motionRecording)
    find_package(motionRecording REQUIRED CONFIG)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MotionConnectorsUtf8CodePage.cmake")

add_executable(vmc_record
    src/Options.cpp
    src/SessionReport.cpp
    src/TraceExport.cpp
    src/main.cpp)
motionconnectors_use_utf8_code_page(vmc_record)

target_compile_features(vmc_record PRIVATE cxx_std_20)
# Only semantic export consumes the downstream writer.
target_link_libraries(vmc_record PRIVATE
    motionConnectorVmc::motionConnectorVmc
    motionRecording::motionRecording)

if(MSVC)
    target_compile_options(vmc_record PRIVATE /utf-8)
    # The pair every target here sets: without NOMINMAX, windows.h's min/max
    # macros break OpenUSD's own numeric_limits<>::max() uses, which arrive
    # through motionCore's Gf value types.
    target_compile_definitions(vmc_record PRIVATE NOMINMAX _USE_MATH_DEFINES)
endif()

install(TARGETS vmc_record RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(VMCRECORD_BUILD_TESTS)
    add_subdirectory(tests)
endif()
