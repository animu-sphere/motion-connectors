# SPDX-License-Identifier: Apache-2.0

# The interpreter this suite runs under, found in this directory's own scope
# (cmake/MotionConnectorsProject.cmake says why).
motionconnectors_find_test_python()

if(NOT Python3_Interpreter_FOUND)
    message(STATUS "motionConnect: Python3 interpreter not found; skipping tests")
    return()
endif()

add_test(NAME motion_connect_list
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_motion_connect.py"
            --tool "$<TARGET_FILE:motion_connect>"
            --mode list)

set(_motionconnect_vmc_capture
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../libs/motionConnectorVmc/tests/corpus/neutral-standing-30hz.vmcpackets")
set(_motionconnect_mocopi_capture
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../libs/motionConnectorMocopi/tests/corpus/neutral-standing-60hz.mocopipackets")
set(_motionconnect_vrchat_capture
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../libs/motionConnectorVrchatOsc/tests/corpus/generated/one-tracker.vrchatoscpackets")

add_test(NAME motion_connect_inspect_vmc
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_motion_connect.py"
            --tool "$<TARGET_FILE:motion_connect>"
            --mode inspect
            --source vmc
            --capture "${_motionconnect_vmc_capture}"
            --profile vmc.v1)
add_test(NAME motion_connect_inspect_mocopi
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_motion_connect.py"
            --tool "$<TARGET_FILE:motion_connect>"
            --mode inspect
            --source mocopi
            --capture "${_motionconnect_mocopi_capture}"
            --profile mocopi.body.v1)
add_test(NAME motion_connect_inspect_vrchat_osc
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_motion_connect.py"
            --tool "$<TARGET_FILE:motion_connect>"
            --mode inspect
            --source vrchat-osc
            --capture "${_motionconnect_vrchat_capture}"
            --profile vrchat-osc.trackers.v1)

add_test(NAME motion_connect_dump_vmc
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_motion_connect.py"
            --tool "$<TARGET_FILE:motion_connect>"
            --mode dump
            --source vmc)
add_test(NAME motion_connect_dump_mocopi
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_motion_connect.py"
            --tool "$<TARGET_FILE:motion_connect>"
            --mode dump
            --source mocopi)
add_test(NAME motion_connect_dump_vrchat_osc
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_motion_connect.py"
            --tool "$<TARGET_FILE:motion_connect>"
            --mode dump
            --source vrchat-osc)
add_test(NAME motion_connect_arguments
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_motion_connect.py"
            --tool "$<TARGET_FILE:motion_connect>"
            --mode arguments)

set(_motionconnect_websocket_capture
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../libs/motionConnectorWebSocket/tests/corpus/messages/steady-60hz.websocketpackets")
add_test(NAME motion_connect_inspect_websocket
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_motion_connect.py"
            --tool "$<TARGET_FILE:motion_connect>" --mode inspect
            --source websocket --capture "${_motionconnect_websocket_capture}" --profile websocket)
add_test(NAME motion_connect_dump_websocket
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_motion_connect.py"
            --tool "$<TARGET_FILE:motion_connect>" --mode dump --source websocket)

foreach(_source vmc mocopi vrchat_osc websocket)
    if(_source STREQUAL "vrchat_osc")
        set(_source_name "vrchat-osc")
        set(_capture "${_motionconnect_vrchat_capture}")
    else()
        set(_source_name "${_source}")
        set(_capture "${_motionconnect_${_source}_capture}")
    endif()
    add_test(NAME motion_connect_bridge_${_source}
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_bridge.py"
                --tool "$<TARGET_FILE:motion_connect>" --mode replay
                --source "${_source_name}" --capture "${_capture}")
endforeach()
add_test(NAME motion_connect_bridge_listen
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_bridge.py"
            --tool "$<TARGET_FILE:motion_connect>" --mode listen
            --source websocket --capture "${_motionconnect_websocket_capture}")
add_test(NAME motion_connect_bridge_live
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_bridge.py"
            --tool "$<TARGET_FILE:motion_connect>" --mode live)
add_test(NAME motion_connect_bridge_limits
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_bridge.py"
            --tool "$<TARGET_FILE:motion_connect>" --mode limits
            --source mocopi --capture "${_motionconnect_mocopi_capture}")
add_test(NAME motion_connect_bridge_errors
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_bridge.py"
            --tool "$<TARGET_FILE:motion_connect>" --mode errors)

get_property(_motionconnect_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${_motionconnect_tests} PROPERTIES TIMEOUT 30)

if(WIN32)
    if(TARGET gf)
        set(_motionconnect_gf gf)
    else()
        set(_motionconnect_gf pxr::gf)
    endif()
    set(_motionconnect_test_path
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_motionconnect_gf}>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_motionconnect_gf}>/../bin")
    get_filename_component(_motionconnect_python_dir "${Python3_EXECUTABLE}"
                           DIRECTORY)
    list(APPEND _motionconnect_test_path
         "PATH=path_list_prepend:${_motionconnect_python_dir}")
    set_tests_properties(${_motionconnect_tests} PROPERTIES
        ENVIRONMENT_MODIFICATION "${_motionconnect_test_path}")
endif()
