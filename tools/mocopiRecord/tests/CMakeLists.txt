# SPDX-License-Identifier: Apache-2.0

# The interpreter this suite runs under, found in this directory's own scope
# (cmake/MotionConnectorsProject.cmake says why).
motionconnectors_find_test_python()

if(NOT Python3_Interpreter_FOUND)
    message(STATUS "mocopiRecord: Python3 interpreter not found; skipping tests")
    return()
endif()

# Four names, split the way every other claim in this adapter is split: what can
# be checked without a socket, what needs one, what needs a socket this runner
# may not have, and what needs bytes a decoder can decode.
#
# Three of the four take no corpus argument, and that absence is still the point
# rather than an omission. The sibling's tests read committed captures because
# its protocol is published and its corpus could be written; this adapter's
# corpus was *measured* off a device afterwards, and the tool that obtained it
# has no opinion about what a datagram means — so its envelope checks author
# their own bytes in Python, which makes the reader check independent of the
# writer exactly as the sibling's Python capture *parser* is.
#
# `mocopi_record_export` is the exception and names its reason: an export
# decodes, so its fixtures have to be things the decoder can decode. Inventing
# those in Python would be a second packet generator, silently disagreeing with
# `tools/generate_packets.py` at whatever point nobody checked.
# The connector owns the corpus; the recorder is a root member here, so the
# path walks up to the repository root rather than to a sibling of the tool.
# Relative to this directory and not to `CMAKE_SOURCE_DIR`: a standalone
# configure of tools/mocopiRecord -- which this tool's CMakeLists.txt supports
# on purpose -- makes that variable the tool's own directory, and the export
# test below would then be handed a corpus path that does not exist.
set(_mocopirecord_corpus
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../libs/motionConnectorMocopi/tests/corpus")

add_test(NAME mocopi_record_inspect
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_mocopi_record.py"
            --tool "$<TARGET_FILE:mocopi_record>"
            --mode inspect)

add_test(NAME mocopi_record_loopback
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_mocopi_record.py"
            --tool "$<TARGET_FILE:mocopi_record>"
            --mode loopback)

add_test(NAME mocopi_record_export
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_mocopi_record.py"
            --tool "$<TARGET_FILE:mocopi_record>"
            --mode export
            --corpus "${_mocopirecord_corpus}")

# The IPv6 warning, on its own name so that "could not check this here" is a word
# in the ctest summary rather than a line nobody sees — the arrangement
# `motionConnectorMocopi_udpReceiverTruncation` established in this adapter for
# exactly the same reason. A runner without IPv6 loopback cannot bind `::1`, and
# inside the loopback suite that would have been invisible: ctest hides a passing
# test's output, so a lane that checked the claim and a lane that quietly declined
# to would print the same line.
#
# The claim is worth a name of its own besides. The vendor documents IPv4 only, so
# an IPv6 bind is a session that can never receive anything — and `UdpReceiver`
# deliberately does not refuse one, because a socket inventing a restriction on
# itself from a *product's* documentation is the wrong layer for it. This tool is
# the right layer, and this test is what makes that split a fact.
add_test(NAME mocopi_record_ipv6
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_mocopi_record.py"
            --tool "$<TARGET_FILE:mocopi_record>"
            --mode ipv6)
set_tests_properties(mocopi_record_ipv6 PROPERTIES SKIP_RETURN_CODE 77)

# The mocopi -> rig end-to-end leg is NOT registered here, and that is a
# decision rather than an omission. Its last step bakes a recorded session onto
# an avatar with a retarget CLI, and this repository has neither and will have
# neither: WORKSPACE.md §2.3 points every edge at usd-motion-plugins and none
# at a consumer. The leg stays in usd-vrm-plugins with the avatar it needs, as
# vmc_record's did and as motion_record_replay's bake did when that tool moved.
# What is here is what this repository can answer for: the recorder's own
# inspect, loopback, export and IPv6 names.

if(WIN32)
    # The same runtime the connector's own tests need, for the same reason:
    # the tool is statically linked against the connector, but motionCore and
    # the sampling and recording packages call exported Gf entry points, so the
    # process loads
    # usd_gf.dll -> usd_tf.dll -> tbb.dll plus python3xx.dll at start-up. An
    # activated `ost` session has them on PATH; a plain-CMake `ctest` does not,
    # and the tool exits 0xC0000135 before main().
    if(TARGET gf)
        set(_mocopirecord_gf gf)
    else()
        set(_mocopirecord_gf pxr::gf)
    endif()
    set(_mocopirecord_test_path
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_mocopirecord_gf}>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_mocopirecord_gf}>/../bin")
    get_filename_component(_mocopirecord_python_dir "${Python3_EXECUTABLE}"
                           DIRECTORY)
    list(APPEND _mocopirecord_test_path
         "PATH=path_list_prepend:${_mocopirecord_python_dir}")
    set_tests_properties(mocopi_record_inspect mocopi_record_loopback
                         mocopi_record_export mocopi_record_ipv6 PROPERTIES
        ENVIRONMENT_MODIFICATION "${_mocopirecord_test_path}")
endif()
