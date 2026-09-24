# SPDX-License-Identifier: Apache-2.0

# The interpreter this suite runs under, found in this directory's own scope
# (cmake/MotionConnectorsProject.cmake says why).
motionconnectors_find_test_python()

if(NOT Python3_Interpreter_FOUND)
    message(STATUS "vrchatOscRecord: Python3 interpreter not found; skipping tests")
    return()
endif()

# The connector owns the corpus; the recorder is a root member here, so the
# path walks up to the repository root rather than to a sibling of the tool.
# Relative to this directory and not to `CMAKE_SOURCE_DIR`: a standalone
# configure of tools/vrchatOscRecord -- which this tool's CMakeLists.txt
# supports on purpose -- makes that variable the tool's own directory.
set(_vrchatoscrecord_corpus
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../libs/motionConnectorVrchatOsc/tests/corpus")

# Four names, split the way every other claim in this adapter is split: what can
# be checked without a socket, what needs one, what needs a socket this runner
# may not have, and what needs bytes a decoder can decode.
#
# Three of the four take no corpus argument, and that is a choice rather than a
# consequence: the generated corpus arrived with VRC-2, and those checks still
# author their own bytes in Python. That is what makes the reader check
# independent of the writer, and what lets the loopback name assert VRC-0's
# done-condition end to end rather than through the C++ that produced it. A tool
# suite fed by this repository's own C++ writer would agree with it by
# construction.
#
# `vrchat_osc_record_export` is the exception and names its reason: an export
# decodes and solves, so its fixtures have to be things the decoder can decode.
# Inventing those in Python would be a second packet generator, silently
# disagreeing with `tools/generate_packets.py` at whatever point nobody checked.
add_test(NAME vrchat_osc_record_inspect
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_vrchat_osc_record.py"
            --tool "$<TARGET_FILE:vrchat_osc_record>"
            --mode inspect)

# Binds loopback sockets on OS-assigned ports, so a lane that forbids binds
# excludes this name and `vrchat_osc_record_ipv6` along with the adapter's three.
add_test(NAME vrchat_osc_record_loopback
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_vrchat_osc_record.py"
            --tool "$<TARGET_FILE:vrchat_osc_record>"
            --mode loopback)

# The IPv6 warning, on its own name so that "could not check this here" is a word
# in the ctest summary rather than a line nobody sees -- the arrangement
# `motionConnectorVrchatOsc_udpReceiverTruncation` uses in this adapter for the same
# reason. A runner without IPv6 loopback cannot bind `::1`.
#
# The claim is worth a name of its own besides. `UdpReceiver` deliberately does
# not refuse an IPv6 bind, because a socket inventing a restriction on itself
# from an *application's* configuration is the wrong layer for it. This tool is
# the right layer, and this test is what makes that split a fact.
add_test(NAME vrchat_osc_record_ipv6
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_vrchat_osc_record.py"
            --tool "$<TARGET_FILE:vrchat_osc_record>"
            --mode ipv6)
set_tests_properties(vrchat_osc_record_ipv6 PROPERTIES SKIP_RETURN_CODE 77)

add_test(NAME vrchat_osc_record_export
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_vrchat_osc_record.py"
            --tool "$<TARGET_FILE:vrchat_osc_record>"
            --mode export
            --corpus "${_vrchatoscrecord_corpus}")

# The VRChat OSC -> rig end-to-end leg is NOT registered here, and that is a
# decision rather than an omission. Its last step bakes a recorded session onto
# an avatar with a retarget CLI, and this repository has neither and will have
# neither: WORKSPACE.md §2.3 points every edge at usd-motion-plugins and none
# at a consumer. The leg stays in usd-vrm-plugins with the avatar it needs, as
# both sibling recorders' did. What is here is what this repository can answer
# for: the recorder's own inspect, loopback, IPv6 and export names -- and the
# export is the one that calls the humanoid solve, so the link the leg was
# waiting for is exercised here even though the bake is not.

if(WIN32)
    # The runtime the adapter's own tests need, for the reason both sibling
    # tools' suites give: the tool is statically linked against the adapter, but
    # motionCore, motionConnectorTracking and motionRuntime call exported Gf entry
    # points, so the process loads usd_gf.dll -> usd_tf.dll -> tbb.dll plus
    # python3xx.dll at start-up. An activated `ost` session has them on PATH; a
    # plain-CMake `ctest` does not, and the tool exits 0xC0000135 before main().
    #
    # **Every name here needs it now, including the three that predate the
    # export.** Until VRC-6 this tool's closure reached no OpenUSD at all, so
    # this block did not exist; linking motionConnectorTracking put usd_gf in front of
    # `--help`.
    if(TARGET gf)
        set(_vrchatoscrecord_gf gf)
    else()
        set(_vrchatoscrecord_gf pxr::gf)
    endif()
    set(_vrchatoscrecord_test_path
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_vrchatoscrecord_gf}>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_vrchatoscrecord_gf}>/../bin")
    get_filename_component(_vrchatoscrecord_python_dir
                           "${Python3_EXECUTABLE}" DIRECTORY)
    list(APPEND _vrchatoscrecord_test_path
         "PATH=path_list_prepend:${_vrchatoscrecord_python_dir}")
    set_tests_properties(vrchat_osc_record_inspect vrchat_osc_record_loopback
                         vrchat_osc_record_ipv6 vrchat_osc_record_export
                         PROPERTIES
        ENVIRONMENT_MODIFICATION "${_vrchatoscrecord_test_path}")
endif()
