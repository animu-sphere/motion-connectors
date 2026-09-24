# SPDX-License-Identifier: Apache-2.0
#
# MotionConnectorsProject.cmake -- the project-level policy every CMake project
# in this repository shares: the root, each library under libs/ and each tool
# under tools/.
#
# Included BEFORE `project()`, because it supplies the version `project()`
# takes. `project()` itself stays a literal call in every CMakeLists.txt:
# CMake requires a top-level listfile to call it directly, and one called from
# a macro draws an author warning and an implicit `project(Project)` that
# enables C. So a component reads:
#
#   cmake_minimum_required(VERSION 3.22)
#   include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/MotionConnectorsProject.cmake")
#   project(motionConnectorOsc
#       VERSION ${MOTIONCONNECTORS_VERSION}
#       DESCRIPTION "..."
#       LANGUAGES CXX)
#   motionconnectors_component_project()
#
# What this module does NOT hold, deliberately: add_library(), add_executable(),
# source lists, target_link_libraries(), find_package() for a dependency, and
# any install rule for a component's own data. The link lines are the
# architecture (WORKSPACE.md §2), and every boundary check reads them from the
# component's own CMakeLists.txt.
#
# Sets:
#   MOTIONCONNECTORS_VERSION   - the repository-root VERSION, the one product
#                                version (WORKSPACE.md §4)
#
# Defines:
#   motionconnectors_project_baseline()   - the C++ baseline
#   motionconnectors_component_project()  - the above, plus the component's
#                                           test option and GNUInstallDirs
#   motionconnectors_find_test_python()   - the interpreter a test lane runs
# and, through MotionConnectorsPackage.cmake,
#   motionconnectors_install_package()    - install/export for one library

include("${CMAKE_CURRENT_LIST_DIR}/MotionConnectorsPackage.cmake")

# The version is re-read on every include rather than behind an include guard:
# a guard would set it only in the first including directory's scope, and a
# standalone configure of a component is its own first include.
get_filename_component(_motionconnectors_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT EXISTS "${_motionconnectors_root}/VERSION")
    message(FATAL_ERROR
        "motion-connectors: no VERSION at ${_motionconnectors_root}. Every "
        "component is configured from inside the repository, whose root VERSION "
        "is the single product version (WORKSPACE.md §4).")
endif()
file(STRINGS "${_motionconnectors_root}/VERSION" MOTIONCONNECTORS_VERSION LIMIT_COUNT 1)
string(STRIP "${MOTIONCONNECTORS_VERSION}" MOTIONCONNECTORS_VERSION)
unset(_motionconnectors_root)

# Single-config generators (Ninja) otherwise leave the build type to the
# compiler's platform default, and with MSVC that default is Debug -- set during
# `project()`, so a check after `project()` never sees it empty. The OpenUSD
# installs this builds against are Release-only, and a Debug build links the
# debug CRT against their release one (LNK2038). So a top-level configure that
# names no build type gets Release, decided here, before `project()`.
# `ost` passes CMAKE_BUILD_TYPE explicitly; a multi-config generator ignores it.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    get_property(_motionconnectors_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(NOT _motionconnectors_multi_config AND NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
    endif()
    unset(_motionconnectors_multi_config)
endif()

# motionconnectors_project_baseline()
#
# C++20, as the siblings build (DEPENDENCIES.md §2). `target_compile_features`
# on each target is the authoritative requirement and travels with the
# exported target; CMAKE_CXX_STANDARD is only the default for a configure that
# names none (`ost`'s toolchain names one), so a plain configure does not
# quietly get the compiler's default.
#
# (The Release default for single-config generators is decided at include
# time, above, because it has to precede `project()`.)
#
# A macro, so the variables land in the caller's directory scope.
macro(motionconnectors_project_baseline)
    if(NOT CMAKE_CXX_STANDARD)
        set(CMAKE_CXX_STANDARD 20)
    endif()
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endmacro()

# motionconnectors_component_project()
#
# Called immediately after a component's `project()`. Applies the baseline,
# includes GNUInstallDirs (after `project()`, which it needs to choose lib64),
# and defines the component's test option:
#
#   <PROJECT_NAME upper-cased>_BUILD_TESTS
#     - set explicitly:                     that value
#     - else, inside the workspace build:   MOTIONCONNECTORS_BUILD_TESTS
#     - else (a standalone configure):      PROJECT_IS_TOP_LEVEL
#
# The component then tests the option by its literal name
# (`if(MOTIONCONNECTOROSC_BUILD_TESTS)`), so a reader of the component sees it.
#
# `enable_testing()` belongs to the top-level project. In the workspace build
# the root owns it; a component configured standalone calls it here, when its
# tests are on.
macro(motionconnectors_component_project)
    if(NOT PROJECT_NAME OR NOT "${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${PROJECT_SOURCE_DIR}")
        message(FATAL_ERROR
            "motionconnectors_component_project() is called right after the "
            "component's own project(), from the same CMakeLists.txt.")
    endif()

    motionconnectors_project_baseline()
    include(GNUInstallDirs)

    string(TOUPPER "${PROJECT_NAME}_BUILD_TESTS" _motionconnectors_tests_option)
    if(NOT DEFINED ${_motionconnectors_tests_option})
        if(DEFINED MOTIONCONNECTORS_BUILD_TESTS)
            set(_motionconnectors_tests_default "${MOTIONCONNECTORS_BUILD_TESTS}")
        else()
            set(_motionconnectors_tests_default "${PROJECT_IS_TOP_LEVEL}")
        endif()
        option(${_motionconnectors_tests_option} "Build ${PROJECT_NAME} tests"
               ${_motionconnectors_tests_default})
        unset(_motionconnectors_tests_default)
    endif()

    if(PROJECT_IS_TOP_LEVEL AND ${_motionconnectors_tests_option})
        enable_testing()
    endif()
    unset(_motionconnectors_tests_option)
endmacro()

# motionconnectors_find_test_python()
#
# Resolves the interpreter a test lane runs under, in the calling directory's
# scope: Python3_EXECUTABLE and Python3_Interpreter_FOUND for the suites that
# read those, and MOTIONCONNECTORS_TEST_PYTHON for the ones that read that.
#
# It must be the Python OpenUSD was built against, and pxrConfig.cmake names
# that one -- it sets Python3_EXECUTABLE unless it is already defined -- so this
# runs after `find_package(pxr)` wherever OpenUSD is resolved. The root keeps
# the result under a name of its own, because pxrConfig.cmake re-finds Python3
# for its Development components in *its* scope, which leaves
# Python3_Interpreter_FOUND false for anything guarded on it later
# (usd-vrm-plugins lost a test that way without a red lane). That is also why
# each tests/ directory calls this itself rather than trusting an inherited
# Python3_Interpreter_FOUND: the find in the caller's own scope is the one
# whose result is true.
#
# A macro, so the variables land in the caller's directory scope.
macro(motionconnectors_find_test_python)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(NOT MOTIONCONNECTORS_TEST_PYTHON AND Python3_Interpreter_FOUND)
        set(MOTIONCONNECTORS_TEST_PYTHON "${Python3_EXECUTABLE}")
    endif()
endmacro()
