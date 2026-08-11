# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Alex.K.

cmake_minimum_required(VERSION 3.16)

set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.mqtt.local")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.mqtt.local")
endif()

# Production security profile (plan S5 #1/#2): only layered in when a
# production build explicitly opts in via ZGW_PRODUCTION_BUILD in the
# environment, so ordinary/development/HIL builds (including the existing
# `firmware-build`/`target-hal-tests-build` CI jobs, which set no such
# variable) are completely unaffected and never silently pick up
# production-only security settings. `idf.py -D SDKCONFIG_DEFAULTS=...` on
# the command line does NOT work for this purpose: this file's own
# unconditional `set(SDKCONFIG_DEFAULTS ...)` above shadows any
# command-line -D override in this directory scope (confirmed against real
# ESP-IDF 5.5 -- a command-line override was silently ignored before this
# env-var gate existed), so the production fragment must be appended here,
# not passed in from outside.
if(DEFINED ENV{ZGW_PRODUCTION_BUILD} AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.production.esp32c6")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.production.esp32c6")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(zigbee_gateway)
