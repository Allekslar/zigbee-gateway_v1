/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "hal_ota.h"
#include "service_runtime.hpp"
#include "version.hpp"

namespace {

hal_ota_https_status_t g_next_status = HAL_OTA_HTTPS_STATUS_OK;
char g_last_url[service::kOtaManifestUrlMaxLen]{};
char g_last_expected_version[service::kOtaManifestVersionMaxLen]{};
char g_last_expected_project[service::kOtaManifestProjectMaxLen]{};

extern "C" int hal_ota_platform_perform_https_update(
    const hal_ota_https_request_t* request,
    hal_ota_progress_cb_t progress_cb,
    void* user_ctx,
    hal_ota_https_result_t* out_result) {
    assert(request != nullptr);
    assert(out_result != nullptr);

    std::memset(g_last_url, 0, sizeof(g_last_url));
    std::memset(g_last_expected_version, 0, sizeof(g_last_expected_version));
    std::memset(g_last_expected_project, 0, sizeof(g_last_expected_project));
    std::strncpy(g_last_url, request->url, sizeof(g_last_url) - 1U);
    if (request->expected_version != nullptr) {
        std::strncpy(g_last_expected_version, request->expected_version, sizeof(g_last_expected_version) - 1U);
    }
    if (request->expected_project_name != nullptr) {
        std::strncpy(g_last_expected_project, request->expected_project_name, sizeof(g_last_expected_project) - 1U);
    }

    std::memset(out_result, 0, sizeof(*out_result));
    out_result->status = g_next_status;
    out_result->bytes_read = 1024U;
    out_result->image_size = 2048U;
    out_result->image_size_known = true;
    std::strncpy(out_result->discovered_version, "2.0.1", sizeof(out_result->discovered_version) - 1U);
    if (progress_cb != nullptr) {
        progress_cb(512U, 2048U, true, user_ctx);
        progress_cb(1024U, 2048U, true, user_ctx);
    }

    out_result->reboot_required = (g_next_status == HAL_OTA_HTTPS_STATUS_OK);
    return g_next_status == HAL_OTA_HTTPS_STATUS_OK ? 0 : -1;
}

service::OtaStartRequest make_request(uint32_t request_id, const char* url, const char* version) {
    service::OtaStartRequest request{};
    request.request_id = request_id;
    std::strncpy(request.manifest.url.data(), url, request.manifest.url.size() - 1U);
    if (version != nullptr) {
        std::strncpy(request.manifest.version.data(), version, request.manifest.version.size() - 1U);
    }
    return request;
}

}  // namespace

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    service::OtaApiSnapshot snapshot{};
    assert(runtime.build_ota_api_snapshot(&snapshot));
    assert(std::strcmp(snapshot.current_version.data(), "host-test") == 0);
    assert(snapshot.stage == service::OtaStage::kIdle);

    const service::OtaStartRequest first_request =
        make_request(41U, "https://updates.local/gateway-v2.bin", "2.0.1");
    assert(runtime.post_ota_start(first_request) == service::OtaSubmitStatus::kAccepted);
    assert(runtime.get_ota_poll_status(first_request.request_id) == service::OtaPollStatus::kQueued);
    assert(runtime.post_ota_start(make_request(42U, "https://updates.local/other.bin", "2.0.2")) ==
           service::OtaSubmitStatus::kBusy);

    assert(runtime.process_pending() == 0U);
    assert(std::strcmp(g_last_url, first_request.manifest.url.data()) == 0);
    assert(std::strcmp(g_last_expected_version, "2.0.1") == 0);
    assert(std::strcmp(g_last_expected_project, common::kProjectName) == 0);
    assert(runtime.get_ota_poll_status(first_request.request_id) == service::OtaPollStatus::kReady);

    assert(runtime.build_ota_api_snapshot(&snapshot));
    assert(snapshot.stage == service::OtaStage::kSwitchPending);
    assert(!snapshot.busy);
    assert(snapshot.downloaded_bytes == 1024U);
    assert(snapshot.image_size == 2048U);
    assert(std::strcmp(snapshot.target_version.data(), "2.0.1") == 0);

    service::OtaResult result{};
    assert(runtime.take_ota_result(first_request.request_id, &result));
    assert(result.status == service::OtaOperationStatus::kOk);
    assert(result.reboot_required);
    assert(result.image_size_known);
    assert(std::strcmp(result.target_version.data(), "2.0.1") == 0);
    assert(runtime.get_ota_poll_status(first_request.request_id) == service::OtaPollStatus::kNotReady);

    g_next_status = HAL_OTA_HTTPS_STATUS_DOWNLOAD_FAILED;
    const service::OtaStartRequest failed_request =
        make_request(43U, "https://updates.local/gateway-v3.bin", "");
    assert(runtime.post_ota_start(failed_request) == service::OtaSubmitStatus::kAccepted);
    assert(runtime.process_pending() == 0U);
    assert(runtime.take_ota_result(failed_request.request_id, &result));
    assert(result.status == service::OtaOperationStatus::kDownloadFailed);
    assert(std::strcmp(result.target_version.data(), "2.0.1") == 0);

    assert(runtime.build_ota_api_snapshot(&snapshot));
    assert(snapshot.stage == service::OtaStage::kFailed);
    assert(snapshot.last_error == service::OtaOperationStatus::kDownloadFailed);
    assert(std::strcmp(snapshot.target_version.data(), "2.0.1") == 0);

    return 0;
}
