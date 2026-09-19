# SPDX-License-Identifier: Apache-2.0
#
# motionConnectorOsc — the OSC 1.0 wire format, and nothing that uses it.
#
# A plain static library: it registers nothing with OpenUSD and ships no
# plugInfo.json (WORKSPACE.md §1.1). Its edge set is **empty**, and emptier than
# `motionConnectorTransport`'s in the one way a reader would not predict: it
# links no platform primitive either. A transport needs a socket and a mutex; a
# decoder is handed a byte range and returns messages, so the only thing below
# that is not this library's own sources is an interpreter to run the boundary
# check with. `tests/check_boundaries.py` enforces exactly that reading, with an
# allowlist that names `motionConnectorOsc` and the CMake keywords and nothing
# else.
#
# Arrived from usd-vrm-plugins' `osc`, with its history (that repository's
# MIG-4; WORKSPACE.md §3). No address literal may appear in it, tests included
# (WORKSPACE.md §2.2).

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION" _osc_version
         LIMIT_COUNT 1)
    string(STRIP "${_osc_version}" _osc_version)
else()
    set(_osc_version "0.1.0")
endif()

project(motionConnectorOsc
    VERSION ${_osc_version}
    DESCRIPTION "OSC 1.0 packet decoding, shared by every OSC-speaking adapter"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED MOTIONCONNECTOROSC_BUILD_TESTS)
    if(DEFINED MOTIONCONNECTORS_BUILD_TESTS)
        set(_osc_tests_default "${MOTIONCONNECTORS_BUILD_TESTS}")
    else()
        set(_osc_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(MOTIONCONNECTOROSC_BUILD_TESTS "Build motionConnectorOsc tests"
           ${_osc_tests_default})
endif()

add_library(motionConnectorOsc STATIC src/OscPacket.cpp)
add_library(motionConnectorOsc::motionConnectorOsc ALIAS motionConnectorOsc)
set_target_properties(motionConnectorOsc PROPERTIES
    EXPORT_NAME motionConnectorOsc
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(motionConnectorOsc PUBLIC cxx_std_17)
target_compile_definitions(motionConnectorOsc PUBLIC MOTIONCONNECTOROSC_STATIC)
target_include_directories(motionConnectorOsc
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")

# No target_link_libraries at all, and the absence is the measurement. The
# transport leaf beside this one links `ws2_32` or `Threads::Threads`; a decoder
# links neither, so the boundary check's allowlist has no platform entry to make
# an exception for.

if(MSVC)
    # PUBLIC for the same reason the connectors use it: the installed headers
    # carry UTF-8 section references, and a consumer on a non-UTF-8 code page
    # warns C4819 without the flag.
    target_compile_options(motionConnectorOsc PUBLIC /utf-8)
endif()

include(GNUInstallDirs)
install(TARGETS motionConnectorOsc
    EXPORT motionConnectorOscTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/motionConnectorOscConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/motionConnectorOscConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionConnectorOsc")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/motionConnectorOscConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion)
install(EXPORT motionConnectorOscTargets
    FILE motionConnectorOscTargets.cmake
    NAMESPACE motionConnectorOsc::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionConnectorOsc")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/motionConnectorOscConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/motionConnectorOscConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionConnectorOsc")

if(MOTIONCONNECTOROSC_BUILD_TESTS)
    enable_testing()
    # The root names the interpreter once, as MOTIONCONNECTORS_TEST_PYTHON,
    # because pxrConfig.cmake leaves Python3_Interpreter_FOUND false in its
    # scope; a standalone build of this library finds its own.
    if(NOT MOTIONCONNECTORS_TEST_PYTHON)
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND)
            set(MOTIONCONNECTORS_TEST_PYTHON "${Python3_EXECUTABLE}")
        endif()
    endif()
    add_subdirectory(tests)
endif()
