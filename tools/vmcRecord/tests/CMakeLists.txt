# SPDX-License-Identifier: Apache-2.0

# The interpreter this suite runs under, found in this directory's own scope
# (cmake/MotionConnectorsProject.cmake says why).
motionconnectors_find_test_python()

if(NOT Python3_Interpreter_FOUND)
    message(STATUS "vmcRecord: Python3 interpreter not found; skipping tests")
    return()
endif()

# The connector owns the corpus; the recorder is a root member here, so the
# path walks up to the repository root rather than to a sibling of the tool.
# Relative to this directory and not to `CMAKE_SOURCE_DIR`: a standalone
# configure of tools/vmcRecord -- which this tool's CMakeLists.txt supports on
# purpose -- makes that variable the tool's own directory, and both tests below
# would then be handed a corpus path that does not exist and fail on an empty
# fixture list.
set(_vmcrecord_corpus
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../libs/motionConnectorVmc/tests/corpus")

# Two tests, split the way every other socket claim in this adapter is split:
# what can be checked from committed bytes, and what needs a socket. The first
# runs anywhere; the second is a separate CTest name so a runner that forbids
# binding excludes it and loses no coverage of the report (roadmap §9.5).
add_test(NAME vmc_record_inspect
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_vmc_record.py"
            --tool "$<TARGET_FILE:vmc_record>"
            --corpus "${_vmcrecord_corpus}"
            --mode inspect)

add_test(NAME vmc_record_loopback
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_vmc_record.py"
            --tool "$<TARGET_FILE:vmc_record>"
            --corpus "${_vmcrecord_corpus}"
            --mode loopback)

# The VMC -> rig end-to-end leg is NOT registered here, and that is a decision
# rather than an omission. Its last step bakes a recorded session onto an
# avatar with a retarget CLI, and this repository has neither and will have
# neither: WORKSPACE.md §2.3 points every edge at usd-motion-plugins and none
# at a consumer. The leg stays in usd-vrm-plugins with the avatar it needs, as
# motion_record_replay's bake did when that tool moved. What is here is what
# this repository can answer for: the recorder's own inspect and loopback.

if(WIN32)
    # The same runtime the adapter's own tests need, for the same reason: the
    # tool is statically linked against the adapter, but motionCore and
    # motionRuntime call exported Gf entry points, so the process loads
    # usd_gf.dll -> usd_tf.dll -> tbb.dll plus python3xx.dll at start-up. An
    # activated `ost` session has them on PATH; a plain-CMake `ctest` does not,
    # and the tool exits 0xC0000135 before main().
    if(TARGET gf)
        set(_vmcrecord_gf gf)
    else()
        set(_vmcrecord_gf pxr::gf)
    endif()
    set(_vmcrecord_test_path
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_vmcrecord_gf}>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_vmcrecord_gf}>/../bin")
    get_filename_component(_vmcrecord_python_dir "${Python3_EXECUTABLE}"
                           DIRECTORY)
    list(APPEND _vmcrecord_test_path
         "PATH=path_list_prepend:${_vmcrecord_python_dir}")
    set_tests_properties(vmc_record_inspect vmc_record_loopback PROPERTIES
        ENVIRONMENT_MODIFICATION "${_vmcrecord_test_path}")
endif()

