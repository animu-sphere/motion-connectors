# SPDX-License-Identifier: Apache-2.0
#
# motion_connect -- the shared-contract CLI.

cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MotionConnectorsProject.cmake")

project(motionConnect
    VERSION ${MOTIONCONNECTORS_VERSION}
    DESCRIPTION "Shared motion connector diagnostic CLI"
    LANGUAGES CXX)

motionconnectors_component_project()

if(NOT TARGET motionConnectorVmc::motionConnectorVmc)
    find_package(motionConnectorVmc REQUIRED CONFIG)
endif()
if(NOT TARGET motionConnectorMocopi::motionConnectorMocopi)
    find_package(motionConnectorMocopi REQUIRED CONFIG)
endif()
if(NOT TARGET motionConnectorVrchatOsc::motionConnectorVrchatOsc)
    find_package(motionConnectorVrchatOsc REQUIRED CONFIG)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MotionConnectorsUtf8CodePage.cmake")

add_executable(motion_connect
    src/Commands.cpp
    src/main.cpp)
motionconnectors_use_utf8_code_page(motion_connect)
target_compile_features(motion_connect PRIVATE cxx_std_20)
target_link_libraries(motion_connect
    PRIVATE
        motionConnectorVmc::motionConnectorVmc
        motionConnectorMocopi::motionConnectorMocopi
        motionConnectorVrchatOsc::motionConnectorVrchatOsc)

if(MSVC)
    target_compile_options(motion_connect PRIVATE /utf-8)
    target_compile_definitions(motion_connect PRIVATE NOMINMAX _USE_MATH_DEFINES)
endif()

install(TARGETS motion_connect RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(MOTIONCONNECT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
