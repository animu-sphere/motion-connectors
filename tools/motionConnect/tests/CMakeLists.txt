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
    set_tests_properties(motion_connect_list motion_connect_inspect_vmc
                         motion_connect_inspect_mocopi
                         motion_connect_inspect_vrchat_osc
                         motion_connect_dump_vmc
                         motion_connect_dump_mocopi
                         motion_connect_dump_vrchat_osc
                         motion_connect_arguments PROPERTIES
        ENVIRONMENT_MODIFICATION "${_motionconnect_test_path}")
endif()
