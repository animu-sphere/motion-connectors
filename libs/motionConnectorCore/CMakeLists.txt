# SPDX-License-Identifier: Apache-2.0
#
# motionConnectorCore -- the shared value contract around a MotionPose.

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION"
         _core_version LIMIT_COUNT 1)
    string(STRIP "${_core_version}" _core_version)
else()
    set(_core_version "0.1.0")
endif()

project(motionConnectorCore
    VERSION ${_core_version}
    DESCRIPTION "Shared motion connector contract and bounded frame buffer"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED MOTIONCONNECTORCORE_BUILD_TESTS)
    if(DEFINED MOTIONCONNECTORS_BUILD_TESTS)
        set(_core_tests_default "${MOTIONCONNECTORS_BUILD_TESTS}")
    else()
        set(_core_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(MOTIONCONNECTORCORE_BUILD_TESTS "Build motionConnectorCore tests"
           ${_core_tests_default})
endif()

if(NOT pxr_FOUND)
    find_package(pxr REQUIRED CONFIG)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/MotionConnectorsOpenUsd.cmake")

if(NOT TARGET motionCore::motionCore)
    find_package(motionCore REQUIRED CONFIG)
endif()

add_library(motionConnectorCore STATIC
    src/FrameBuffer.cpp)
add_library(motionConnectorCore::motionConnectorCore ALIAS motionConnectorCore)
set_target_properties(motionConnectorCore PROPERTIES
    EXPORT_NAME motionConnectorCore
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(motionConnectorCore PUBLIC cxx_std_17)
target_compile_definitions(motionConnectorCore PUBLIC MOTIONCONNECTORCORE_STATIC)
target_include_directories(motionConnectorCore
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
target_link_libraries(motionConnectorCore PUBLIC motionCore::motionCore)

if(MSVC)
    target_compile_options(motionConnectorCore PUBLIC /utf-8)
endif()

include(GNUInstallDirs)
install(TARGETS motionConnectorCore
    EXPORT motionConnectorCoreTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/motionConnectorCoreConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/motionConnectorCoreConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionConnectorCore")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/motionConnectorCoreConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion)
install(EXPORT motionConnectorCoreTargets
    FILE motionConnectorCoreTargets.cmake
    NAMESPACE motionConnectorCore::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionConnectorCore")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/motionConnectorCoreConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/motionConnectorCoreConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionConnectorCore")

if(MOTIONCONNECTORCORE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()