# SPDX-License-Identifier: Apache-2.0
#
# Three executables, and the small number is deliberate.
#
# The socket, the capture format and the connectors' diagnostic tables are
# covered exhaustively by the connectors' own suites, which name their own
# protocol's behaviour and exercise this library through it. They arrive with
# each connector (WORKSPACE.md §3). Copying them here would make a third copy
# of the very thing usd-vrm-plugins' OSC-2 removed two of.
#
# What is here is what could not exist before: the internal header's two
# decisions, which no connector could hold without diverging from its sibling,
# and the vehicle carrying a code set that belongs to no connector.
#
# The third arrived later and by the same rule. The `p` line -- which peer sent
# a record -- was added to the format after the extraction, and it is transport
# identity rather than protocol: no connector's suite owns it, and a decoder
# may not read it. Its test names a magic that is nobody's.

# The interpreter this suite runs under, found in this directory's own scope
# (cmake/MotionConnectorsProject.cmake says why).
motionconnectors_find_test_python()

# NDEBUG compiles `assert()` away, and the root CMakeLists defaults
# single-config builds to Release -- so a suite built the normal way would print
# "verified" and return 0 without having checked anything. Undefine it for the
# test targets only: the library under test stays Release, the checks stay live.
# usd-vrm-plugins' motionRuntime suite hit this first.
set(_transport_assertions $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)

# The debt usd-vrm-plugins' OSC-1 recorded: the poll timeout mapping and the
# wake-up predicate, neither of which a socket-owning suite can produce on
# demand.
add_executable(motionConnectorTransport_pollTimeout_tests test_poll_timeout.cpp)
# src/ is on this target's include path rather than the library's PUBLIC one:
# PollTimeout.h is internal and is not installed, and a consumer that could
# include it would make it an API.
target_include_directories(motionConnectorTransport_pollTimeout_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")

# The diagnostic vehicle, against a code set this library has never seen.
add_executable(motionConnectorTransport_diagnostics_tests test_diagnostics.cpp)

# The capture format's one line that belongs to no adapter, and the
# compatibility claim two corpora rest on: a capture whose records name no
# peer is written exactly as it was before the line existed.
add_executable(motionConnectorTransport_packetCapture_tests test_packet_capture.cpp)

foreach(_transport_test motionConnectorTransport_pollTimeout_tests
                             motionConnectorTransport_diagnostics_tests
                             motionConnectorTransport_packetCapture_tests)
    target_link_libraries(${_transport_test}
        PRIVATE motionConnectorTransport::motionConnectorTransport)
    target_compile_options(${_transport_test}
        PRIVATE ${_transport_assertions})
    if(MSVC)
        # The headers cite roadmap sections by their `§` sign, and MSVC reads
        # source in the *system* code page unless told otherwise -- so a
        # Japanese-Windows build warns C4819 on every include without this.
        target_compile_options(${_transport_test} PRIVATE /utf-8)
    endif()
endforeach()

add_test(NAME motionConnectorTransport_pollTimeout
         COMMAND motionConnectorTransport_pollTimeout_tests)
add_test(NAME motionConnectorTransport_diagnostics
         COMMAND motionConnectorTransport_diagnostics_tests)
add_test(NAME motionConnectorTransport_packetCapture
         COMMAND motionConnectorTransport_packetCapture_tests)

# No ENVIRONMENT_MODIFICATION for the runtime's bin/ on any of the three, and
# here that is a statement rather than an oversight: this library links no
# OpenUSD on any platform, so there is nothing for a loader to find. The
# boundary check below asserts exactly that against the built binary.

if(MOTIONCONNECTORS_TEST_PYTHON)
    # Source *and* binary. A check whose library reaches motionCore's Gf value
    # types has to allow the OpenUSD libraries three toolchains disagree about
    # recording (usd-vrm-plugins' motionBvh check measured it). Here the edge
    # set is empty, so the binary half has no allowlist and no platform variance
    # to survive: no `usd_*` import may appear, on any of the three.
    add_test(NAME motionConnectorTransport_boundaries
        COMMAND "${MOTIONCONNECTORS_TEST_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_boundaries.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/.."
            "$<TARGET_FILE:motionConnectorTransport_diagnostics_tests>")
endif()
