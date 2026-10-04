# SPDX-License-Identifier: Apache-2.0
#
# Two executables: the codec's own suite, built from frames in code, and the
# corpus reader, which replays the generated messages under tests/corpus/
# against their expectations. The corpus is what FRAME_WIRE_FORMAT §6 asks
# for -- every refusal has a committed message behind it -- and the suite is
# what the corpus cannot carry: frames a test builds from values, encoder
# refusals, the size bound and the process locale.

# The interpreter this suite runs under, found in this directory's own scope
# (cmake/MotionConnectorsProject.cmake says why).
motionconnectors_find_test_python()

# NDEBUG compiles `assert()` away, and the root CMakeLists defaults
# single-config builds to Release -- so a suite built the normal way would
# print "passed" and return 0 without having checked anything. Undefine it for
# the test targets only.
set(_wire_assertions $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)

add_executable(motionConnectorWire_frameWire_tests test_frame_wire.cpp)
add_executable(motionConnectorWire_corpus_tests test_corpus.cpp)
foreach(_wire_test motionConnectorWire_frameWire_tests motionConnectorWire_corpus_tests)
    target_link_libraries(${_wire_test} PRIVATE motionConnectorWire::motionConnectorWire)
    target_compile_options(${_wire_test} PRIVATE ${_wire_assertions})
    if(MSVC)
        target_compile_options(${_wire_test} PRIVATE /utf-8)
    endif()
endforeach()

set(_wire_corpus "${CMAKE_CURRENT_SOURCE_DIR}/corpus")
add_test(NAME motionConnectorWire_frameWire COMMAND motionConnectorWire_frameWire_tests)
add_test(NAME motionConnectorWire_corpus
    COMMAND motionConnectorWire_corpus_tests "${_wire_corpus}")
set(_wire_tests motionConnectorWire_frameWire motionConnectorWire_corpus)

if(WIN32)
    if(TARGET gf)
        set(_wire_gf gf)
    else()
        set(_wire_gf pxr::gf)
    endif()
    # The codec is a static library, but `motionCore` calls exported Gf entry
    # points, so the test executables load usd_gf.dll at run time -- which
    # pulls usd_tf.dll -> tbb.dll from bin/ plus python3xx.dll. An activated
    # `ost` session has all three on PATH; a plain-CMake `ctest` run does not,
    # and the test exits 0xC0000135 without them.
    set(_wire_test_path
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_wire_gf}>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_wire_gf}>/../bin")
    if(Python3_EXECUTABLE)
        get_filename_component(_wire_python_dir "${Python3_EXECUTABLE}" DIRECTORY)
        list(APPEND _wire_test_path "PATH=path_list_prepend:${_wire_python_dir}")
    endif()
    set_tests_properties(${_wire_tests} PROPERTIES
        ENVIRONMENT_MODIFICATION "${_wire_test_path}")
endif()

if(MOTIONCONNECTORS_TEST_PYTHON)
    # The test executable, not the archive: a static library records no
    # imports, so inspecting it would be a check that cannot fail.
    add_test(NAME motionConnectorWire_boundaries
        COMMAND "${MOTIONCONNECTORS_TEST_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_boundaries.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/.."
            "$<TARGET_FILE:motionConnectorWire_frameWire_tests>")

    # motionConnectorWire_corpus proves the committed messages are what the
    # C++ writer and reader say they are; this proves they are still what the
    # generator writes, so a hand-edited message cannot stay green.
    add_test(NAME motionConnectorWire_messageGen
        COMMAND "${MOTIONCONNECTORS_TEST_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../tools/generate_messages.py"
            --check --output "${_wire_corpus}")
endif()
