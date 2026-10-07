# SPDX-License-Identifier: Apache-2.0
add_executable(motionConnectorOpenXR_connector_tests test_connector.cpp)
target_link_libraries(motionConnectorOpenXR_connector_tests PRIVATE
    motionConnectorOpenXR::motionConnectorOpenXR)
target_compile_options(motionConnectorOpenXR_connector_tests PRIVATE
    $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)
add_test(NAME motionConnectorOpenXR_connector COMMAND motionConnectorOpenXR_connector_tests)
if(WIN32)
    if(TARGET gf)
        set(_openxr_gf gf)
    else()
        set(_openxr_gf pxr::gf)
    endif()
    set(_openxr_path
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_openxr_gf}>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_openxr_gf}>/../bin"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:OpenXR::openxr_loader>")
    if(Python3_EXECUTABLE)
        get_filename_component(_openxr_python "${Python3_EXECUTABLE}" DIRECTORY)
        list(APPEND _openxr_path "PATH=path_list_prepend:${_openxr_python}")
    endif()
    set_tests_properties(motionConnectorOpenXR_connector PROPERTIES
        ENVIRONMENT_MODIFICATION "${_openxr_path}")
endif()
