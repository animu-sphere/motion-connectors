# SPDX-License-Identifier: Apache-2.0
#
#   _layout        every ctypes struct, prototype and enum against the C header
#   _api           refusals, lifetimes, absence, strings, threads. No socket
#   _replayParity  the binding against the C++ connectors, on the corpus
#   _loopback      a live VMC session over a loopback socket, an OS-chosen port
#
# The installed package runs in workspace_installed_consumer. The parity suite
# needs every source, so it exists only when all four are built.

set(_python_binding_tests motionConnectorPython_layout motionConnectorPython_api)
foreach(_test IN ITEMS layout:LayoutTest api:ApiTest)
    string(REPLACE ":" ";" _test "${_test}")
    list(GET _test 0 _name)
    list(GET _test 1 _class)
    add_test(NAME motionConnectorPython_${_name}
        COMMAND "${MOTIONCONNECTORS_TEST_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_binding.py" ${_class})
endforeach()

if(MOTIONCONNECTORS_BUILD_VMC AND MOTIONCONNECTORS_BUILD_MOCOPI
   AND MOTIONCONNECTORS_BUILD_VRCHAT_OSC AND MOTIONCONNECTORS_BUILD_WEBSOCKET)
    add_executable(motionConnectorPython_replay_oracle replay_oracle.cpp)
    target_link_libraries(motionConnectorPython_replay_oracle
        PRIVATE
            motionConnectorVmc::motionConnectorVmc
            motionConnectorMocopi::motionConnectorMocopi
            motionConnectorVrchatOsc::motionConnectorVrchatOsc
            motionConnectorWebSocket::motionConnectorWebSocket)
    target_compile_features(motionConnectorPython_replay_oracle PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(motionConnectorPython_replay_oracle PRIVATE /utf-8)
        target_compile_definitions(motionConnectorPython_replay_oracle PRIVATE NOMINMAX)
    endif()
    add_test(NAME motionConnectorPython_replayParity
        COMMAND "${MOTIONCONNECTORS_TEST_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_binding.py" ReplayParityTest)
    add_test(NAME motionConnectorPython_loopback
        COMMAND "${MOTIONCONNECTORS_TEST_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_binding.py" LoopbackTest)
    list(APPEND _python_binding_tests
        motionConnectorPython_replayParity motionConnectorPython_loopback)
    set_tests_properties(motionConnectorPython_replayParity PROPERTIES
        ENVIRONMENT
            "MOTION_CONNECTORS_TEST_ORACLE=$<TARGET_FILE:motionConnectorPython_replay_oracle>")
endif()

# PATH on the same terms as the C ABI's tests: the library pulls OpenUSD's gf,
# and through it tbb and python3xx, none of which sit beside it.
set(_python_binding_environment
    "PYTHONPATH=set:${CMAKE_CURRENT_SOURCE_DIR}/.."
    "PYTHONDONTWRITEBYTECODE=set:1"
    "MOTION_CONNECTORS_LIBRARY=set:$<TARGET_FILE:motionConnectorCAbi>")
if(WIN32)
    if(TARGET gf)
        set(_python_binding_gf gf)
    else()
        set(_python_binding_gf pxr::gf)
    endif()
    list(APPEND _python_binding_environment
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_python_binding_gf}>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_python_binding_gf}>/../bin")
endif()
set_tests_properties(${_python_binding_tests} PROPERTIES
    ENVIRONMENT_MODIFICATION "${_python_binding_environment}")
