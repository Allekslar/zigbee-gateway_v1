# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Alex.K.

cmake_minimum_required(VERSION 3.16)

set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.mqtt.local")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.mqtt.local")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(zigbee_gateway)
