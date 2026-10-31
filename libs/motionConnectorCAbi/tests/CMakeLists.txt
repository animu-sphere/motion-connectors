# SPDX-License-Identifier: Apache-2.0
#
#   _conformance   the ABI against the C++ connectors, on the corpus: replay
#                  parity, refusals, struct sizes, lifetimes, strings. No socket.
#   _loopback      a live VMC session over a loopback socket, an OS-chosen port
#   _cConsumer     a consumer in C, linking this library alone; frozen layouts
#   _exports       the library exports exactly what the header declares
#
# The conformance suite needs every source, so it exists only when all four are
# built. The consumer and the export check hold for any selection.

motionconnectors_find_test_python()

add_executable(motionConnectorCAbi_c_consumer_tests test_c_consumer.c)
set_target_properties(motionConnectorCAbi_c_consumer_tests PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
target_link_libraries(motionConnectorCAbi_c_consumer_tests PRIVATE motionConnectorCAbi)
if(MSVC)
    target_compile_options(motionConnectorCAbi_c_consumer_tests PRIVATE /utf-8 /W4 /WX)
else()
    target_compile_options(motionConnectorCAbi_c_consumer_tests PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME motionConnectorCAbi_cConsumer COMMAND motionConnectorCAbi_c_consumer_tests)
set(_cabi_tests motionConnectorCAbi_cConsumer)

if(MOTIONCONNECTORS_BUILD_VMC AND MOTIONCONNECTORS_BUILD_MOCOPI
   AND MOTIONCONNECTORS_BUILD_VRCHAT_OSC AND MOTIONCONNECTORS_BUILD_WEBSOCKET)
    add_executable(motionConnectorCAbi_tests test_c_abi.cpp)
    # The connectors here are a second, direct copy to compare with; they share
    # no state with the ones inside the shared library.
    target_link_libraries(motionConnectorCAbi_tests
        PRIVATE
            motionConnectorCAbi
            motionConnectorVmc::motionConnectorVmc
            motionConnectorMocopi::motionConnectorMocopi
            motionConnectorVrchatOsc::motionConnectorVrchatOsc
            motionConnectorWebSocket::motionConnectorWebSocket)
    target_compile_features(motionConnectorCAbi_tests PRIVATE cxx_std_20)
    # NDEBUG compiles `assert()` away, and the root defaults to Release.
    target_compile_options(motionConnectorCAbi_tests PRIVATE
        $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)
    if(MSVC)
        target_compile_options(motionConnectorCAbi_tests PRIVATE /utf-8)
        target_compile_definitions(motionConnectorCAbi_tests PRIVATE NOMINMAX)
    endif()
    if(WIN32)
        target_link_libraries(motionConnectorCAbi_tests PRIVATE ws2_32)
    endif()

    set(_cabi_corpus "${CMAKE_CURRENT_SOURCE_DIR}/../..")
    add_test(NAME motionConnectorCAbi_conformance
        COMMAND motionConnectorCAbi_tests "${_cabi_corpus}")
    add_test(NAME motionConnectorCAbi_loopback
        COMMAND motionConnectorCAbi_tests "${_cabi_corpus}" --loopback)
    list(APPEND _cabi_tests motionConnectorCAbi_conformance motionConnectorCAbi_loopback)
endif()

if(WIN32)
    if(TARGET gf)
        set(_cabi_gf gf)
    else()
        set(_cabi_gf pxr::gf)
    endif()
    # The library loads usd_gf.dll at run time, which pulls usd_tf.dll -> tbb.dll
    # from bin/ plus python3xx.dll. An activated `ost` session has all three on
    # PATH; a plain-CMake `ctest` run does not. The library itself sits in its
    # own build directory.
    set(_cabi_test_path
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:motionConnectorCAbi>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_cabi_gf}>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_cabi_gf}>/../bin")
    if(Python3_EXECUTABLE)
        get_filename_component(_cabi_python_dir "${Python3_EXECUTABLE}" DIRECTORY)
        list(APPEND _cabi_test_path "PATH=path_list_prepend:${_cabi_python_dir}")
    endif()
    set_tests_properties(${_cabi_tests} PROPERTIES
        ENVIRONMENT_MODIFICATION "${_cabi_test_path}")
endif()

if(MOTIONCONNECTORS_TEST_PYTHON)
    set(_cabi_export_args "")
    if(WIN32)
        set(_cabi_export_args --linker "${CMAKE_LINKER}")
    endif()
    add_test(NAME motionConnectorCAbi_exports
        COMMAND "${MOTIONCONNECTORS_TEST_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_exports.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/../include/motionConnectorCAbi/MotionConnector.h"
            "$<TARGET_FILE:motionConnectorCAbi>"
            ${_cabi_export_args})
endif()
