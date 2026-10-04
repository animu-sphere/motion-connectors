# SPDX-License-Identifier: Apache-2.0
#
# Three executables, five names (WEBSOCKET_CONNECTOR.md §9):
#
#   _rfc6455         the codec on bytes alone: the accept key, length encoding,
#                    the bound, two sessions in memory
#   _framingCorpus   the same binary over corpus/framing/: one byte stream per
#                    row of §5 and per refusal of §4
#   _connector       the code table, Open's refusals, §7 through PushMessage
#   _messageCorpus   the same binary over corpus/messages/: §7's restart, gap,
#                    regression and profile rows, replayed from captures
#   _loopback        both directions in both roles over loopback sockets
#
# Only `_loopback` binds a socket, so a lane that forbids binds excludes that
# one name. Every bind it makes is loopback on an OS-chosen port.

motionconnectors_find_test_python()

# NDEBUG compiles `assert()` away, and the root CMakeLists defaults
# single-config builds to Release. Undefine it for the test targets only.
set(_websocket_assertions $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)

add_executable(motionConnectorWebSocket_rfc6455_tests test_rfc6455.cpp)
add_executable(motionConnectorWebSocket_connector_tests test_connector.cpp)
add_executable(motionConnectorWebSocket_loopback_tests test_loopback.cpp)
foreach(_websocket_test
        motionConnectorWebSocket_rfc6455_tests
        motionConnectorWebSocket_connector_tests
        motionConnectorWebSocket_loopback_tests)
    target_link_libraries(${_websocket_test}
        PRIVATE motionConnectorWebSocket::motionConnectorWebSocket)
    # The private layers are tested where they are (§10): the codec on bytes,
    # and the TCP layer as a loopback test's own peer.
    target_include_directories(${_websocket_test} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    target_compile_options(${_websocket_test} PRIVATE ${_websocket_assertions})
    if(MSVC)
        target_compile_options(${_websocket_test} PRIVATE /utf-8)
    endif()
endforeach()

set(_websocket_corpus "${CMAKE_CURRENT_SOURCE_DIR}/corpus")
add_test(NAME motionConnectorWebSocket_rfc6455 COMMAND motionConnectorWebSocket_rfc6455_tests)
add_test(NAME motionConnectorWebSocket_framingCorpus
    COMMAND motionConnectorWebSocket_rfc6455_tests "${_websocket_corpus}/framing")
add_test(NAME motionConnectorWebSocket_connector COMMAND motionConnectorWebSocket_connector_tests)
add_test(NAME motionConnectorWebSocket_messageCorpus
    COMMAND motionConnectorWebSocket_connector_tests "${_websocket_corpus}/messages")
add_test(NAME motionConnectorWebSocket_loopback COMMAND motionConnectorWebSocket_loopback_tests)
set(_websocket_tests
    motionConnectorWebSocket_rfc6455 motionConnectorWebSocket_framingCorpus
    motionConnectorWebSocket_connector motionConnectorWebSocket_messageCorpus
    motionConnectorWebSocket_loopback)

if(WIN32)
    if(TARGET gf)
        set(_websocket_gf gf)
    else()
        set(_websocket_gf pxr::gf)
    endif()
    # The connector links motionCore, so the test executables load usd_gf.dll
    # at run time -- which pulls usd_tf.dll -> tbb.dll from bin/ plus
    # python3xx.dll. An activated `ost` session has all three on PATH; a
    # plain-CMake `ctest` run does not, and the test exits 0xC0000135.
    set(_websocket_test_path
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_websocket_gf}>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_websocket_gf}>/../bin")
    if(Python3_EXECUTABLE)
        get_filename_component(_websocket_python_dir "${Python3_EXECUTABLE}" DIRECTORY)
        list(APPEND _websocket_test_path "PATH=path_list_prepend:${_websocket_python_dir}")
    endif()
    set_tests_properties(${_websocket_tests} PROPERTIES
        ENVIRONMENT_MODIFICATION "${_websocket_test_path}")
endif()

if(MOTIONCONNECTORS_TEST_PYTHON)
    # The loopback suite, not the archive: a static library records no
    # imports, and this is the binary that reaches every layer, sockets
    # included, so a new file reaching a new layer is covered here.
    add_test(NAME motionConnectorWebSocket_boundaries
        COMMAND "${MOTIONCONNECTORS_TEST_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_boundaries.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/.."
            "$<TARGET_FILE:motionConnectorWebSocket_loopback_tests>")

    # The corpus tests prove the committed bytes are what the session and the
    # connector make of them; this proves they are still what the generator
    # writes, so a hand-edited stream cannot stay green.
    add_test(NAME motionConnectorWebSocket_corpusGen
        COMMAND "${MOTIONCONNECTORS_TEST_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../tools/generate_corpus.py"
            --check --output "${_websocket_corpus}")
endif()
