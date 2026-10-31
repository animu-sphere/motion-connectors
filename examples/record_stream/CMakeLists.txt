# SPDX-License-Identifier: Apache-2.0
#
# record_stream -- connector -> MotionFrame -> downstream intake and recording.
#
# Self-contained, because an example is what a consumer copies: outside this
# repository it configures against installed packages alone, and the
# installed-consumer lane builds it that way. It is a directory, not a workspace
# member, so nothing here is packaged or installed.

cmake_minimum_required(VERSION 3.22)

project(recordStream
    DESCRIPTION "Record a connector's poses through the downstream motion intake"
    LANGUAGES CXX)

foreach(_package IN ITEMS motionConnectorVmc motionConnectorMocopi motionConnectorWebSocket
                          motionRecording)
    if(NOT TARGET ${_package}::${_package})
        find_package(${_package} REQUIRED CONFIG)
    endif()
endforeach()

add_executable(record_stream main.cpp)
target_compile_features(record_stream PRIVATE cxx_std_20)
# The acquisition/motion join is this executable's. No connector library gains
# the motionRecording edge (docs/architecture/WORKSPACE.md §2.1).
target_link_libraries(record_stream PRIVATE
    motionConnectorVmc::motionConnectorVmc
    motionConnectorMocopi::motionConnectorMocopi
    motionConnectorWebSocket::motionConnectorWebSocket
    motionRecording::motionRecording)

if(MSVC)
    target_compile_options(record_stream PRIVATE /utf-8)
    target_compile_definitions(record_stream PRIVATE NOMINMAX _USE_MATH_DEFINES)
endif()

# In the workspace only: outside it the helper does not exist, and a consumer's
# paths are the consumer's to handle.
if(COMMAND motionconnectors_use_utf8_code_page)
    motionconnectors_use_utf8_code_page(record_stream)
endif()

if(MOTIONCONNECTORS_BUILD_TESTS AND MOTIONCONNECTORS_TEST_PYTHON)
    # The recorders are the independent oracle for the parity checks; the suite
    # requires them, so they are built whenever the example's tests are.
    set(_record_stream_tests "")
    foreach(_mode IN ITEMS replay restart intake observations arguments loopback)
        add_test(NAME record_stream_${_mode}
            COMMAND "${MOTIONCONNECTORS_TEST_PYTHON}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_record_stream.py"
                --tool "$<TARGET_FILE:record_stream>"
                --vmc-record "$<TARGET_FILE:vmc_record>"
                --mocopi-record "$<TARGET_FILE:mocopi_record>"
                --root "${PROJECT_SOURCE_DIR}/../.."
                --mode ${_mode})
        list(APPEND _record_stream_tests record_stream_${_mode})
    endforeach()
    set_tests_properties(${_record_stream_tests} PROPERTIES LABELS "consumer-integration")

    if(WIN32)
        # OpenUSD's gf closure and the Python it was built against, which the
        # executable loads before main() and which an inactive shell lacks.
        if(TARGET gf)
            set(_record_stream_gf gf)
        else()
            set(_record_stream_gf pxr::gf)
        endif()
        get_filename_component(_record_stream_python_dir "${MOTIONCONNECTORS_TEST_PYTHON}"
                               DIRECTORY)
        set_tests_properties(${_record_stream_tests} PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_record_stream_gf}>;PATH=path_list_prepend:$<TARGET_FILE_DIR:${_record_stream_gf}>/../bin;PATH=path_list_prepend:${_record_stream_python_dir}")
    endif()
endif()
