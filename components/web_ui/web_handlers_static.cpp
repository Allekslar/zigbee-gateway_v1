/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "web_routes.hpp"

#include <cstddef>
#include <cstdint>
#include <inttypes.h>

#ifdef ESP_PLATFORM
#include <cerrno>
#include <sys/socket.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "log_tags.h"
#endif

namespace web_ui {

namespace {

constexpr std::size_t kStaticChunkedThresholdBytes = 4096U;
constexpr std::size_t kStaticChunkSizeBytes = 1024U;

#ifdef ESP_PLATFORM
constexpr TickType_t kStaticSendRetryDelayTicks = pdMS_TO_TICKS(25);
constexpr uint32_t kStaticSendTimeoutRetryLimit = 4U;

int static_asset_send_with_retry(httpd_handle_t hd, int sockfd, const char* buf, size_t buf_len, int flags) {
    if (buf == nullptr) {
        return HTTPD_SOCK_ERR_INVALID;
    }

    for (uint32_t attempt = 0; attempt <= kStaticSendTimeoutRetryLimit; ++attempt) {
        const int ret = send(sockfd, buf, buf_len, flags);
        if (ret >= 0) {
            if (attempt > 0) {
                ESP_LOGI(
                    LOG_TAG_WEB_SERVER,
                    "static asset send recovered after retry sockfd=%d attempt=%" PRIu32,
                    sockfd,
                    attempt);
            }
            return ret;
        }

        if (errno == EAGAIN || errno == EINTR) {
            if (attempt == kStaticSendTimeoutRetryLimit) {
                ESP_LOGW(
                    LOG_TAG_WEB_SERVER,
                    "static asset send retry budget exhausted sockfd=%d errno=%d",
                    sockfd,
                    errno);
                return HTTPD_SOCK_ERR_TIMEOUT;
            }
            vTaskDelay(kStaticSendRetryDelayTicks);
            continue;
        }

        switch (errno) {
            case EINVAL:
            case EBADF:
            case EFAULT:
            case ENOTSOCK:
                return HTTPD_SOCK_ERR_INVALID;
            default:
                return HTTPD_SOCK_ERR_FAIL;
        }
    }

    return HTTPD_SOCK_ERR_FAIL;
}
#endif

extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[] asm("_binary_index_html_gz_end");
extern const uint8_t style_css_gz_start[] asm("_binary_style_css_gz_start");
extern const uint8_t style_css_gz_end[] asm("_binary_style_css_gz_end");
extern const uint8_t app_js_gz_start[] asm("_binary_app_js_gz_start");
extern const uint8_t app_js_gz_end[] asm("_binary_app_js_gz_end");

esp_err_t send_embedded_file_chunked(
    httpd_req_t* req,
    const uint8_t* start,
    std::size_t file_size,
    int sockfd) noexcept {
    (void)sockfd;
    const std::size_t total_chunks =
        (file_size + kStaticChunkSizeBytes - 1U) / kStaticChunkSizeBytes;

    for (std::size_t chunk_index = 0U; chunk_index < total_chunks; ++chunk_index) {
        const std::size_t offset = chunk_index * kStaticChunkSizeBytes;
        const std::size_t chunk_size = ((file_size - offset) < kStaticChunkSizeBytes)
            ? (file_size - offset)
            : kStaticChunkSizeBytes;
        const esp_err_t chunk_result = httpd_resp_send_chunk(
            req,
            reinterpret_cast<const char*>(start + offset),
            static_cast<ssize_t>(chunk_size));
        if (chunk_result != ESP_OK) {
#ifdef ESP_PLATFORM
            ESP_LOGW(
                LOG_TAG_WEB_SERVER,
                "send_embedded_file chunk failed uri=%s sockfd=%d chunk=%zu/%zu offset=%zu size=%zu err=%s",
                req->uri != nullptr ? req->uri : "?",
                sockfd,
                chunk_index + 1U,
                total_chunks,
                offset,
                chunk_size,
                esp_err_to_name(chunk_result));
#endif
            return chunk_result;
        }
    }

    return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t send_embedded_file(
    httpd_req_t* req,
    const char* content_type,
    const char* cache_control,
    const uint8_t* start,
    const uint8_t* end) noexcept {
    if (req == nullptr || content_type == nullptr || cache_control == nullptr || start == nullptr ||
        end == nullptr || end < start) {
        return ESP_FAIL;
    }

    std::size_t file_size = static_cast<std::size_t>(end - start);
    int sockfd = -1;
#ifdef ESP_PLATFORM
    sockfd = httpd_req_to_sockfd(req);
    if (sockfd >= 0) {
        const esp_err_t override_result =
            httpd_sess_set_send_override(req->handle, sockfd, &static_asset_send_with_retry);
        if (override_result != ESP_OK) {
            ESP_LOGW(
                LOG_TAG_WEB_SERVER,
                "failed to install static send override uri=%s sockfd=%d err=%s",
                req->uri != nullptr ? req->uri : "?",
                sockfd,
                esp_err_to_name(override_result));
        }
    }
#endif
    (void)httpd_resp_set_type(req, content_type);
    (void)httpd_resp_set_hdr(req, "Cache-Control", cache_control);
    (void)httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    (void)httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
    const bool use_chunked_send = file_size > kStaticChunkedThresholdBytes;
    const esp_err_t send_result = use_chunked_send
        ? send_embedded_file_chunked(req, start, file_size, sockfd)
        : httpd_resp_send(
              req,
              reinterpret_cast<const char*>(start),
              static_cast<ssize_t>(file_size));
    if (send_result != ESP_OK) {
#ifdef ESP_PLATFORM
        ESP_LOGW(
            LOG_TAG_WEB_SERVER,
            "send_embedded_file uri=%s sockfd=%d size=%zu mode=%s failed: %s",
            req->uri != nullptr ? req->uri : "?",
            sockfd,
            file_size,
            use_chunked_send ? "chunked" : "single",
            esp_err_to_name(send_result));
#endif
        return send_result;
    }
    return ESP_OK;
}

esp_err_t root_get_handler(httpd_req_t* req) {
    return send_embedded_file(
        req,
        "text/html; charset=utf-8",
        "no-store, max-age=0",
        index_html_gz_start,
        index_html_gz_end);
}

esp_err_t index_html_get_handler(httpd_req_t* req) {
    return send_embedded_file(
        req,
        "text/html; charset=utf-8",
        "no-store, max-age=0",
        index_html_gz_start,
        index_html_gz_end);
}

esp_err_t style_css_get_handler(httpd_req_t* req) {
    return send_embedded_file(
        req,
        "text/css; charset=utf-8",
        "public, max-age=31536000, immutable",
        style_css_gz_start,
        style_css_gz_end);
}

esp_err_t app_js_get_handler(httpd_req_t* req) {
    return send_embedded_file(
        req,
        "application/javascript; charset=utf-8",
        "public, max-age=31536000, immutable",
        app_js_gz_start,
        app_js_gz_end);
}

esp_err_t favicon_get_handler(httpd_req_t* req) {
    if (req == nullptr) {
        return ESP_FAIL;
    }

    (void)httpd_resp_set_status(req, "204 No Content");
    (void)httpd_resp_set_type(req, "image/x-icon");
    (void)httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, nullptr, 0);
}

}  // namespace

bool register_static_routes(void* server_handle, WebRouteContext* context) noexcept {
    if (server_handle == nullptr || context == nullptr) {
        return false;
    }

    httpd_uri_t root_get_uri{};
    root_get_uri.uri = "/";
    root_get_uri.method = HTTP_GET;
    root_get_uri.handler = root_get_handler;
    root_get_uri.user_ctx = context;

    httpd_uri_t index_html_get_uri{};
    index_html_get_uri.uri = "/index.html";
    index_html_get_uri.method = HTTP_GET;
    index_html_get_uri.handler = index_html_get_handler;
    index_html_get_uri.user_ctx = context;

    httpd_uri_t style_css_get_uri{};
    style_css_get_uri.uri = "/style.css";
    style_css_get_uri.method = HTTP_GET;
    style_css_get_uri.handler = style_css_get_handler;
    style_css_get_uri.user_ctx = context;

    httpd_uri_t app_js_get_uri{};
    app_js_get_uri.uri = "/app.js";
    app_js_get_uri.method = HTTP_GET;
    app_js_get_uri.handler = app_js_get_handler;
    app_js_get_uri.user_ctx = context;

    httpd_uri_t favicon_get_uri{};
    favicon_get_uri.uri = "/favicon.ico";
    favicon_get_uri.method = HTTP_GET;
    favicon_get_uri.handler = favicon_get_handler;
    favicon_get_uri.user_ctx = context;

    auto handle = static_cast<httpd_handle_t>(server_handle);
    if (httpd_register_uri_handler(handle, &root_get_uri) != ESP_OK) {
        return false;
    }
    if (httpd_register_uri_handler(handle, &index_html_get_uri) != ESP_OK) {
        return false;
    }
    if (httpd_register_uri_handler(handle, &style_css_get_uri) != ESP_OK) {
        return false;
    }
    if (httpd_register_uri_handler(handle, &app_js_get_uri) != ESP_OK) {
        return false;
    }
    if (httpd_register_uri_handler(handle, &favicon_get_uri) != ESP_OK) {
        return false;
    }

    return true;
}

}  // namespace web_ui
