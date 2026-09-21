# SPDX-License-Identifier: Apache-2.0
#
# motion_connect -- the shared-contract CLI.

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION"
         _motionconnect_version LIMIT_COUNT 1)
    string(STRIP "${_motionconnect_version}" _motionconnect_version)
else()
    set(_motionconnect_version "0.1.0")
endif()

project(motionConnectTool
    VERSION ${_motionconnect_version}
    DESCRIPTION "Shared motion connector diagnostic CLI"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED MOTIONCONNECT_BUILD_TESTS)
    if(DEFINED MOTIONCONNECTORS_BUILD_TESTS)
        set(_motionconnect_tests_default "${MOTIONCONNECTORS_BUILD_TESTS}")
    else()
        set(_motionconnect_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(MOTIONCONNECT_BUILD_TESTS "Build motion_connect tests"
           ${_motionconnect_tests_default})
endif()

if(NOT TARGET motionConnectorVmc::motionConnectorVmc)
    find_package(motionConnectorVmc REQUIRED CONFIG)
endif()
if(NOT TARGET motionConnectorMocopi::motionConnectorMocopi)
    find_package(motionConnectorMocopi REQUIRED CONFIG)
endif()
if(NOT TARGET motionConnectorVrchatOsc::motionConnectorVrchatOsc)
    find_package(motionConnectorVrchatOsc REQUIRED CONFIG)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/MotionConnectorsUtf8CodePage.cmake")

add_executable(motion_connect
    src/Commands.cpp
    src/main.cpp)
motionconnectors_use_utf8_code_page(motion_connect)
target_compile_features(motion_connect PRIVATE cxx_std_17)
target_link_libraries(motion_connect
    PRIVATE
        motionConnectorVmc::motionConnectorVmc
        motionConnectorMocopi::motionConnectorMocopi
        motionConnectorVrchatOsc::motionConnectorVrchatOsc)

if(MSVC)
    target_compile_options(motion_connect PRIVATE /utf-8)
    target_compile_definitions(motion_connect PRIVATE NOMINMAX _USE_MATH_DEFINES)
endif()

set_target_properties(motion_connect PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(motion_connect PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_CURRENT_SOURCE_DIR}/bin")
endforeach()

include(GNUInstallDirs)
install(TARGETS motion_connect RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(MOTIONCONNECT_BUILD_TESTS)
    if(Python3_Interpreter_FOUND)
        enable_testing()
        add_subdirectory(tests)
    endif()
endif()