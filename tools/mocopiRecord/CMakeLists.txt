# SPDX-License-Identifier: Apache-2.0
#
# mocopi_record — the mocopi adapter's CLI.
#
# A workspace member of its own, added from the repository root: WORKSPACE.md
# §2.1 puts every tool under the root tools/, beside motion_connect. It still
# ships *with* its connector -- §5 names one artifact carrying the library
# together with its CLI -- and a standalone configure of this directory
# resolves that library as an installed package, which is the configuration the
# artifact would be built from.

cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MotionConnectorsProject.cmake")

project(mocopiRecord
    VERSION ${MOTIONCONNECTORS_VERSION}
    DESCRIPTION "Native mocopi session recorder"
    LANGUAGES CXX)

motionconnectors_component_project()

# A standalone configure of this directory resolves the adapter as an installed
# package; the composed build already has the in-tree target.
if(NOT TARGET motionConnectorMocopi::motionConnectorMocopi)
    find_package(motionConnectorMocopi REQUIRED CONFIG)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MotionConnectorsUtf8CodePage.cmake")

add_executable(mocopi_record
    src/Options.cpp
    src/SessionReport.cpp
    src/TraceExport.cpp
    src/main.cpp)
motionconnectors_use_utf8_code_page(mocopi_record)

target_compile_features(mocopi_record PRIVATE cxx_std_20)
# The connector, and deliberately nothing else. §2 would permit this target to
# author a stage the way a consumer's retarget CLI does; it does not need to,
# and a link line is the cheapest place to keep that true.
# `ws2_32` arrives transitively because the adapter declares it PUBLIC, which is
# right: the socket this tool drives is the adapter's.
target_link_libraries(mocopi_record PRIVATE motionConnectorMocopi::motionConnectorMocopi)

if(MSVC)
    target_compile_options(mocopi_record PRIVATE /utf-8)
    # The pair every target here sets: without NOMINMAX, windows.h's min/max
    # macros break OpenUSD's own numeric_limits<>::max() uses, which arrive
    # through motionCore's Gf value types.
    target_compile_definitions(mocopi_record PRIVATE NOMINMAX _USE_MATH_DEFINES)
endif()

install(TARGETS mocopi_record RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(MOCOPIRECORD_BUILD_TESTS)
    add_subdirectory(tests)
endif()
