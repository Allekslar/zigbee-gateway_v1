# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Alex.K.

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/ProjectCompileOptions.cmake")

idf_component_register(
    SRCS "utils.cpp"
    INCLUDE_DIRS "include"
)

zg_apply_component_compile_policy(${COMPONENT_LIB})
