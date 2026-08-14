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
        "web_handlers_rcp.cpp"
        "web_handlers_v1.cpp"
        "web_handler_common.cpp"
        "web_v1_common.cpp"
    INCLUDE_DIRS
        "include"
        "."
    REQUIRES core service common app_hal esp_http_server esp_https_server esp_timer
)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    set(WEB_UI_ASSET_GZ_DIR "${CMAKE_CURRENT_BINARY_DIR}/assets_gz")
    set(WEB_UI_GZIP_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/../../scripts/gzip_asset.py")

    set(WEB_UI_INDEX_HTML_GZ "${WEB_UI_ASSET_GZ_DIR}/index.html.gz")
    set(WEB_UI_STYLE_CSS_GZ "${WEB_UI_ASSET_GZ_DIR}/style.css.gz")
    set(WEB_UI_APP_JS_GZ "${WEB_UI_ASSET_GZ_DIR}/app.js.gz")

    add_custom_command(
        OUTPUT
            "${WEB_UI_INDEX_HTML_GZ}"
            "${WEB_UI_STYLE_CSS_GZ}"
            "${WEB_UI_APP_JS_GZ}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${WEB_UI_ASSET_GZ_DIR}"
        COMMAND "${Python3_EXECUTABLE}" "${WEB_UI_GZIP_SCRIPT}" "${CMAKE_CURRENT_LIST_DIR}/assets/index.html" "${WEB_UI_INDEX_HTML_GZ}"
        COMMAND "${Python3_EXECUTABLE}" "${WEB_UI_GZIP_SCRIPT}" "${CMAKE_CURRENT_LIST_DIR}/assets/style.css" "${WEB_UI_STYLE_CSS_GZ}"
        COMMAND "${Python3_EXECUTABLE}" "${WEB_UI_GZIP_SCRIPT}" "${CMAKE_CURRENT_LIST_DIR}/assets/app.js" "${WEB_UI_APP_JS_GZ}"
        DEPENDS
            "${WEB_UI_GZIP_SCRIPT}"
            "${CMAKE_CURRENT_LIST_DIR}/assets/index.html"
            "${CMAKE_CURRENT_LIST_DIR}/assets/style.css"
            "${CMAKE_CURRENT_LIST_DIR}/assets/app.js"
        VERBATIM
    )

    add_custom_target(web_ui_assets_gz
        DEPENDS
            "${WEB_UI_INDEX_HTML_GZ}"
            "${WEB_UI_STYLE_CSS_GZ}"
            "${WEB_UI_APP_JS_GZ}"
    )

    add_dependencies(${COMPONENT_LIB} web_ui_assets_gz)
    target_add_binary_data(${COMPONENT_LIB} "${WEB_UI_INDEX_HTML_GZ}" BINARY)
    target_add_binary_data(${COMPONENT_LIB} "${WEB_UI_STYLE_CSS_GZ}" BINARY)
    target_add_binary_data(${COMPONENT_LIB} "${WEB_UI_APP_JS_GZ}" BINARY)
endif()

zg_apply_component_compile_policy(${COMPONENT_LIB})
