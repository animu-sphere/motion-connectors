# SPDX-License-Identifier: Apache-2.0
#
# motionConnectorWebSocket -- `MotionFrame` over WebSocket, both directions,
# listening or connecting (docs/design/WEBSOCKET_CONNECTOR.md).
#
# A plain static library: it registers nothing with OpenUSD and ships no
# plugInfo.json (WORKSPACE.md §1.1). Its edges are WORKSPACE §2.1's line:
# `motionConnectorCore` for the contract it implements, `motionConnectorWire`
# for the message, and `motionConnectorTransport` for the diagnostic vehicle
# and the capture format. RFC 6455, SHA-1 and base64 are implemented in src/,
# so there is no third-party library (§2), and the OS's sockets are the only
# platform edge.
#
# Inside it, three private layers (§10): a socket-free RFC 6455 codec
# (Rfc6455, Session), a non-blocking TCP layer (Tcp), and what both directions
# share above them (Endpoint). None is installed; each becomes a library when
# another component needs it.

cmake_minimum_required(VERSION 3.22)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MotionConnectorsProject.cmake")

project(motionConnectorWebSocket
    VERSION ${MOTIONCONNECTORS_VERSION}
    DESCRIPTION "MotionFrame over WebSocket: openstrata.motion.frame/v1 in both directions"
    LANGUAGES CXX)

motionconnectors_component_project()

if(NOT pxr_FOUND)
    find_package(pxr REQUIRED CONFIG)
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MotionConnectorsOpenUsd.cmake")

if(NOT TARGET motionConnectorTransport::motionConnectorTransport)
    find_package(motionConnectorTransport REQUIRED CONFIG)
endif()
if(NOT TARGET motionCore::motionCore)
    find_package(motionCore REQUIRED CONFIG)
endif()
if(NOT TARGET motionConnectorCore::motionConnectorCore)
    find_package(motionConnectorCore REQUIRED CONFIG)
endif()
if(NOT TARGET motionConnectorWire::motionConnectorWire)
    find_package(motionConnectorWire REQUIRED CONFIG)
endif()

add_library(motionConnectorWebSocket STATIC
    src/Connector.cpp
    src/Diagnostics.cpp
    src/Endpoint.cpp
    src/FrameSender.cpp
    src/Rfc6455.cpp
    src/Session.cpp
    src/Tcp.cpp)
add_library(motionConnectorWebSocket::motionConnectorWebSocket ALIAS motionConnectorWebSocket)
set_target_properties(motionConnectorWebSocket PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(motionConnectorWebSocket PUBLIC cxx_std_20)
target_compile_definitions(motionConnectorWebSocket PUBLIC MOTIONCONNECTORWEBSOCKET_STATIC)
target_include_directories(motionConnectorWebSocket
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        # The codec, the TCP layer and the endpoint are private (§10); the
        # tests reach them here, and nothing installs them.
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

# The three edges, PUBLIC because the installed headers name a type from each.
target_link_libraries(motionConnectorWebSocket
    PUBLIC
        motionConnectorCore::motionConnectorCore
        motionConnectorWire::motionConnectorWire
        motionConnectorTransport::motionConnectorTransport)

# The OS's sockets, which are not a dependency direction. The transport links
# the same library PUBLIC; naming it here keeps this library's own socket calls
# from depending on that.
if(WIN32)
    target_link_libraries(motionConnectorWebSocket PRIVATE ws2_32)
endif()

if(MSVC)
    target_compile_options(motionConnectorWebSocket PUBLIC /utf-8)
endif()

motionconnectors_install_package(TARGET motionConnectorWebSocket)

if(MOTIONCONNECTORWEBSOCKET_BUILD_TESTS)
    add_subdirectory(tests)
endif()
