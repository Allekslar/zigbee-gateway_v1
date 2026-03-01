# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Alex.K.

cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../components/app_hal"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/common"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/core"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/service"
    "${CMAKE_CURRENT_LIST_DIR}/../../components/web_ui"
)

add_compile_definitions(SERVICE_RUNTIME_TEST_HOOKS=1)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hal_target_tests)
