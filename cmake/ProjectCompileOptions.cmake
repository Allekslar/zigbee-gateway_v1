# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Alex.K.

include_guard(GLOBAL)

function(zg_apply_component_compile_policy target_name)
    if (NOT TARGET "${target_name}")
        message(FATAL_ERROR "Target '${target_name}' does not exist.")
    endif()

    target_compile_options("${target_name}" PRIVATE
        -Wall
        -Wextra
        -Werror
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
        $<$<CONFIG:Release>:-O2>
        $<$<CONFIG:Release>:-flto>
    )

    target_link_options("${target_name}" PRIVATE
        $<$<CONFIG:Release>:-flto>
    )
endfunction()
