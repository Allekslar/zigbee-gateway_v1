# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Alex.K.

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/ProjectCompileOptions.cmake")

idf_component_register(
    SRCS
        "web_server.cpp"
        "web_routes.cpp"
        "web_handlers_static.cpp"
        "web_handlers_device.cpp"
        "web_handlers_network.cpp"
        "web_handlers_config.cpp"
        "web_handlers_ota.cpp"
        "web_handler_common.cpp"
    INCLUDE_DIRS "include"
    REQUIRES core service common esp_http_server esp_timer
    EMBED_TXTFILES
        "assets/index.html"
        "assets/style.css"
        "assets/app.js"
)

zg_apply_component_compile_policy(${COMPONENT_LIB})
