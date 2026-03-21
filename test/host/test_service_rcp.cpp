/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "core_registry.hpp"
#include "effect_executor.hpp"
#include "service_runtime.hpp"
#include "version.hpp"

namespace {

int g_begin_status = 0;
int g_write_status = 0;
int g_end_status = 0;
uint32_t g_last_write_len = 0U;

extern "C" int hal_rcp_stack_update_begin(void) {
    return g_begin_status;
}

extern "C" int hal_rcp_stack_update_write(const uint8_t* data, uint32_t len) {
    assert(data != nullptr);
    g_last_write_len = len;
    return g_write_status;
}

extern "C" int hal_rcp_stack_update_end(void) {
    return g_end_status;
}

service::RcpUpdateRequest make_request(uint32_t request_id, const char* url, const char* version) {
    service::RcpUpdateRequest request{};
    request.request_id = request_id;
    std::strncpy(request.url.data(), url, request.url.size() - 1U);
    if (version != nullptr) {
        std::strncpy(request.target_version.data(), version, request.target_version.size() - 1U);
    }
    std::strncpy(request.transport.data(), "uart", request.transport.size() - 1U);
    return request;
}

}  // namespace

int main() {
    core::CoreRegistry registry;
    service::EffectExecutor effect_executor;
    service::ServiceRuntime runtime(registry, effect_executor);

    service::RcpUpdateApiSnapshot snapshot{};
    assert(runtime.build_rcp_update_api_snapshot(&snapshot));
    assert(snapshot.stage == service::RcpUpdateStage::kIdle);

    const service::RcpUpdateRequest first_request =
        make_request(51U, "https://updates.local/rcp-v2.bin", "rcp-2.0.0");
    assert(runtime.post_rcp_update_start(first_request) == service::RcpUpdateSubmitStatus::kAccepted);
    assert(runtime.get_rcp_update_poll_status(first_request.request_id) == service::RcpUpdatePollStatus::kQueued);
    assert(runtime.post_rcp_update_start(make_request(52U, "https://updates.local/rcp-v3.bin", "rcp-3.0.0")) ==
           service::RcpUpdateSubmitStatus::kBusy);

    assert(runtime.process_pending() == 0U);
    assert(g_last_write_len == std::strlen(first_request.url.data()));
    assert(runtime.get_rcp_update_poll_status(first_request.request_id) == service::RcpUpdatePollStatus::kReady);
    assert(runtime.build_rcp_update_api_snapshot(&snapshot));
    assert(snapshot.stage == service::RcpUpdateStage::kCompleted);
    assert(snapshot.written_bytes == g_last_write_len);
    assert(std::strcmp(snapshot.current_version.data(), common::kVersion) == 0);
    assert(std::strcmp(snapshot.target_version.data(), "rcp-2.0.0") == 0);

    service::RcpUpdateResult result{};
    assert(runtime.take_rcp_update_result(first_request.request_id, &result));
    assert(result.status == service::RcpUpdateOperationStatus::kOk);
    assert(result.written_bytes == g_last_write_len);
    assert(std::strcmp(result.target_version.data(), "rcp-2.0.0") == 0);
    assert(runtime.get_rcp_update_poll_status(first_request.request_id) == service::RcpUpdatePollStatus::kNotReady);

    g_begin_status = 0;
    g_write_status = -1;
    g_end_status = 0;
    const service::RcpUpdateRequest failed_request =
        make_request(53U, "https://updates.local/rcp-v4.bin", "rcp-4.0.0");
    assert(runtime.post_rcp_update_start(failed_request) == service::RcpUpdateSubmitStatus::kAccepted);
    assert(runtime.process_pending() == 0U);
    assert(runtime.take_rcp_update_result(failed_request.request_id, &result));
    assert(result.status == service::RcpUpdateOperationStatus::kWriteFailed);
    assert(runtime.build_rcp_update_api_snapshot(&snapshot));
    assert(snapshot.stage == service::RcpUpdateStage::kFailed);
    assert(snapshot.last_error == service::RcpUpdateOperationStatus::kWriteFailed);

    const service::OtaStartRequest ota_request = [] {
        service::OtaStartRequest request{};
        request.request_id = 99U;
        std::strncpy(request.manifest.url.data(), "https://updates.local/gateway.bin", request.manifest.url.size() - 1U);
        std::strncpy(request.manifest.version.data(), "9.9.9", request.manifest.version.size() - 1U);
        return request;
    }();
    assert(runtime.post_rcp_update_start(make_request(54U, "https://updates.local/rcp-v5.bin", "rcp-5.0.0")) ==
           service::RcpUpdateSubmitStatus::kAccepted);
    assert(runtime.post_ota_start(ota_request) == service::OtaSubmitStatus::kBusy);

    return 0;
}
