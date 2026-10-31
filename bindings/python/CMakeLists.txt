# SPDX-License-Identifier: Apache-2.0
#
# The Python binding over the acquisition C ABI (DESIGN_POLICY §19). Pure Python:
# it links nothing and loads the library at run time, so nothing here compiles
# except the test oracle. It installs where OpenUSD puts `pxr`, under the
# library directory, and finds its library from there.

install(DIRECTORY motion_connectors
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/python"
    FILES_MATCHING PATTERN "*.py" PATTERN "py.typed"
    PATTERN "__pycache__" EXCLUDE)

if(NOT MOTIONCONNECTORS_BUILD_TESTS OR NOT MOTIONCONNECTORS_TEST_PYTHON)
    return()
endif()

add_subdirectory(tests)
