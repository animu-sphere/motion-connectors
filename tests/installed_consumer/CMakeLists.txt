# SPDX-License-Identifier: Apache-2.0
#
# A consumer of the installed packages, configured from a copy outside the
# repository with only the install prefix and OpenUSD on CMAKE_PREFIX_PATH
# (scripts/check_installed_consumer.py). It names no workspace target: every
# package is found through its installed config, at the version the
# repository's VERSION says, and linked through its exported target.
cmake_minimum_required(VERSION 3.22)
project(MotionConnectorsInstalledConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MOTIONCONNECTORS_CONSUMER_VERSION)
    message(FATAL_ERROR "Pass -DMOTIONCONNECTORS_CONSUMER_VERSION=<major.minor>")
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/packages.json" _packages_json)
string(JSON _count LENGTH "${_packages_json}" packages)

set(_targets "")
set(_includes "")
if(_count GREATER 0)
    math(EXPR _last "${_count} - 1")
    foreach(_index RANGE ${_last})
        string(JSON _name GET "${_packages_json}" packages ${_index} name)
        string(JSON _header GET "${_packages_json}" packages ${_index} header)
        # The packages are written with SameMinorVersion compatibility.
        find_package(${_name} ${MOTIONCONNECTORS_CONSUMER_VERSION} CONFIG REQUIRED)
        if(NOT TARGET ${_name}::${_name})
            message(FATAL_ERROR
                "find_package(${_name}) succeeded but defines no ${_name}::${_name}")
        endif()
        list(APPEND _targets ${_name}::${_name})
        string(APPEND _includes "#include \"${_header}\"\n")
    endforeach()
endif()

# One translation unit that includes every package's public header, so a
# header that reaches for something the package does not install fails here.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/includes.h" "${_includes}")
add_executable(installed_consumer main.cpp)
target_include_directories(installed_consumer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(installed_consumer PRIVATE
    MOTIONCONNECTORS_CONSUMER_PACKAGES=${_count})
target_link_libraries(installed_consumer PRIVATE ${_targets})
