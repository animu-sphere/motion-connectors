# SPDX-License-Identifier: Apache-2.0
#
# motionConnectorCore -- the shared value contract around a MotionPose.

cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MotionConnectorsProject.cmake")

project(motionConnectorCore
    VERSION ${MOTIONCONNECTORS_VERSION}
    DESCRIPTION "Shared motion connector contract and bounded frame buffer"
    LANGUAGES CXX)

motionconnectors_component_project()

if(NOT pxr_FOUND)
    find_package(pxr REQUIRED CONFIG)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MotionConnectorsOpenUsd.cmake")

if(NOT TARGET motionCore::motionCore)
    find_package(motionCore REQUIRED CONFIG)
endif()

add_library(motionConnectorCore STATIC
    src/FrameBuffer.cpp)
add_library(motionConnectorCore::motionConnectorCore ALIAS motionConnectorCore)
set_target_properties(motionConnectorCore PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(motionConnectorCore PUBLIC cxx_std_20)
target_compile_definitions(motionConnectorCore PUBLIC MOTIONCONNECTORCORE_STATIC)
target_include_directories(motionConnectorCore
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
target_link_libraries(motionConnectorCore PUBLIC motionCore::motionCore)

if(MSVC)
    target_compile_options(motionConnectorCore PUBLIC /utf-8)
endif()

motionconnectors_install_package(TARGET motionConnectorCore)

if(MOTIONCONNECTORCORE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
