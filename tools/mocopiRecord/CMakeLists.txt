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

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION"
         _mocopirecord_version LIMIT_COUNT 1)
    string(STRIP "${_mocopirecord_version}" _mocopirecord_version)
else()
    set(_mocopirecord_version "0.1.0")
endif()

project(mocopiRecordTool
    VERSION ${_mocopirecord_version}
    DESCRIPTION "Native mocopi session recorder"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED MOCOPIRECORD_BUILD_TESTS)
    if(DEFINED MOTIONCONNECTORMOCOPI_BUILD_TESTS)
        set(_mocopirecord_tests_default "${MOTIONCONNECTORMOCOPI_BUILD_TESTS}")
    else()
        set(_mocopirecord_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(MOCOPIRECORD_BUILD_TESTS "Build mocopi_record tests"
           ${_mocopirecord_tests_default})
endif()

# A standalone configure of this directory resolves the adapter as an installed
# package; the composed build already has the in-tree target.
if(NOT TARGET motionConnectorMocopi::motionConnectorMocopi)
    find_package(motionConnectorMocopi REQUIRED CONFIG)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/MotionConnectorsUtf8CodePage.cmake")

add_executable(mocopi_record
    src/Options.cpp
    src/SessionReport.cpp
    src/TraceExport.cpp
    src/main.cpp)
motionconnectors_use_utf8_code_page(mocopi_record)

target_compile_features(mocopi_record PRIVATE cxx_std_17)
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

# Staged into the tool's own bin/, the way `vmc_record` is: `ost plugin package` reads the directories openstrata.tool.yaml
# declares rather than a build tree, so a tool that only lands in build/ is
# refused at packaging with PRECONDITION_FAILED.
set_target_properties(mocopi_record PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(mocopi_record PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_CURRENT_SOURCE_DIR}/bin")
endforeach()

include(GNUInstallDirs)
install(TARGETS mocopi_record RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(MOCOPIRECORD_BUILD_TESTS)
    enable_testing()
    if(NOT Python3_Interpreter_FOUND)
        find_package(Python3 COMPONENTS Interpreter QUIET)
    endif()
    add_subdirectory(tests)
endif()
