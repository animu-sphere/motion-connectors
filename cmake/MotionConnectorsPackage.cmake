# SPDX-License-Identifier: Apache-2.0
#
# MotionConnectorsPackage.cmake -- how a library of this repository installs as
# a CMake package.
#
# motionconnectors_install_package(
#     TARGET <target>
#     [NAMESPACE <ns>::]              default: <target>::
#     [COMPATIBILITY <mode>])         default: SameMinorVersion
#
# Installs, for one library target:
#   - the target, exported as <ns><target> (EXPORT_NAME is the target's name)
#   - its public headers, include/ -> ${CMAKE_INSTALL_INCLUDEDIR}
#   - <target>Config.cmake, configured from the component's own
#     cmake/<target>Config.cmake.in
#   - <target>ConfigVersion.cmake, at PROJECT_VERSION
#   - <target>Targets.cmake
# all under ${CMAKE_INSTALL_LIBDIR}/cmake/<target>.
#
# What stays in the component: the Config.cmake.in itself, because which
# `find_dependency` calls it makes is the package's public dependency set
# (each connector's boundary check reads it against the link line), and any
# data the component installs beside its package (a source profile, say).
#
# SameMinorVersion is the default because the product is pre-1.0: a 0.2 is
# free to break a 0.1 consumer, and every descriptor under tools/ requires
# ">=0.1,<0.2" of the libraries it links.
#
# Each call records the package in the global MOTIONCONNECTORS_PACKAGES
# property, which is how the installed-consumer lane knows which packages this
# configure installs when a connector is switched off.

include(CMakePackageConfigHelpers)

function(motionconnectors_install_package)
    cmake_parse_arguments(PARSE_ARGV 0 _pkg "" "TARGET;NAMESPACE;COMPATIBILITY" "")
    if(_pkg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "motionconnectors_install_package: unexpected arguments: ${_pkg_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT _pkg_TARGET OR NOT TARGET ${_pkg_TARGET})
        message(FATAL_ERROR
            "motionconnectors_install_package: TARGET '${_pkg_TARGET}' is not a target")
    endif()
    if(NOT _pkg_NAMESPACE)
        set(_pkg_NAMESPACE "${_pkg_TARGET}::")
    endif()
    if(NOT _pkg_COMPATIBILITY)
        set(_pkg_COMPATIBILITY SameMinorVersion)
    endif()

    set(_name "${_pkg_TARGET}")
    set(_config_in "${CMAKE_CURRENT_SOURCE_DIR}/cmake/${_name}Config.cmake.in")
    if(NOT EXISTS "${_config_in}")
        message(FATAL_ERROR
            "motionconnectors_install_package: ${_name} has no ${_config_in}")
    endif()
    # GNUInstallDirs is included by motionconnectors_component_project(); the
    # package destination is only meaningful after it.
    if(NOT CMAKE_INSTALL_LIBDIR)
        message(FATAL_ERROR
            "motionconnectors_install_package: call motionconnectors_component_project() first")
    endif()
    set(_destination "${CMAKE_INSTALL_LIBDIR}/cmake/${_name}")

    set_target_properties(${_name} PROPERTIES EXPORT_NAME ${_name})

    install(TARGETS ${_name}
        EXPORT ${_name}Targets
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

    configure_package_config_file(
        "${_config_in}"
        "${CMAKE_CURRENT_BINARY_DIR}/${_name}Config.cmake"
        INSTALL_DESTINATION "${_destination}")
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/${_name}ConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY ${_pkg_COMPATIBILITY})
    install(EXPORT ${_name}Targets
        FILE ${_name}Targets.cmake
        NAMESPACE ${_pkg_NAMESPACE}
        DESTINATION "${_destination}")
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/${_name}Config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/${_name}ConfigVersion.cmake"
        DESTINATION "${_destination}")

    set_property(GLOBAL APPEND PROPERTY MOTIONCONNECTORS_PACKAGES ${_name})
endfunction()
