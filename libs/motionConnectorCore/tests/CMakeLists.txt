# SPDX-License-Identifier: Apache-2.0

set(_motionconnectorcore_assertions
    $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)

add_executable(motionConnectorCore_frameBuffer_tests test_frame_buffer.cpp)
target_link_libraries(motionConnectorCore_frameBuffer_tests
    PRIVATE motionConnectorCore::motionConnectorCore)
target_compile_options(motionConnectorCore_frameBuffer_tests
    PRIVATE ${_motionconnectorcore_assertions})
if(MSVC)
    target_compile_options(motionConnectorCore_frameBuffer_tests PRIVATE /utf-8)
endif()
add_test(NAME motionConnectorCore_frameBuffer
    COMMAND motionConnectorCore_frameBuffer_tests)