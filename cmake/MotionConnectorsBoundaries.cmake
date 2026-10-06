# SPDX-License-Identifier: Apache-2.0
# Check PRIVATE static links, imported interfaces, aliases and targets named
# inside generator expressions. Inspect all configurations conservatively:
# forbidden edges may not hide behind conditions false on this runner.
include_guard(GLOBAL)

function(motionconnectors_check_target_boundary root)
    set(_pending "${root}")
    set(_visited "")
    while(_pending)
        list(POP_FRONT _pending _target)
        if(_target IN_LIST _visited)
            continue()
        endif()
        list(APPEND _visited "${_target}")
        if(NOT TARGET "${_target}")
            continue()
        endif()
        get_target_property(_alias "${_target}" ALIASED_TARGET)
        if(_alias)
            list(APPEND _pending "${_alias}")
        endif()
        set(_properties LINK_LIBRARIES INTERFACE_LINK_LIBRARIES
            INTERFACE_LINK_LIBRARIES_DIRECT IMPORTED_LINK_INTERFACE_LIBRARIES
            IMPORTED_LINK_DEPENDENT_LIBRARIES IMPORTED_LOCATION IMPORTED_IMPLIB)
        get_target_property(_configs "${_target}" IMPORTED_CONFIGURATIONS)
        foreach(_config IN LISTS _configs)
            string(TOUPPER "${_config}" _config)
            foreach(_property IMPORTED_LINK_INTERFACE_LIBRARIES
                    IMPORTED_LINK_DEPENDENT_LIBRARIES IMPORTED_LOCATION IMPORTED_IMPLIB)
                list(APPEND _properties "${_property}_${_config}")
            endforeach()
        endforeach()
        foreach(_property IN LISTS _properties)
            get_target_property(_edges "${_target}" "${_property}")
            if(NOT _edges)
                continue()
            endif()
            string(TOLOWER "${_edges}" _lower)
            if(_lower MATCHES "motion(sampling|recording|retarget|usd)")
                message(FATAL_ERROR
                    "Connector boundary: ${root} reaches forbidden downstream motion dependency "
                    "through ${_target}.${_property}: ${_edges}")
            endif()
            string(REGEX REPLACE "\\$<[A-Za-z_][A-Za-z0-9_]*:" " " _names "${_edges}")
            string(REGEX MATCHALL "[A-Za-z_][A-Za-z0-9_.:+/-]*" _tokens "${_names}")
            foreach(_token IN LISTS _tokens)
                if(TARGET "${_token}")
                    list(APPEND _pending "${_token}")
                endif()
            endforeach()
        endforeach()
    endwhile()
endfunction()
