/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "operation_result_store.hpp"

namespace {

service::NetworkResult make_result(uint32_t request_id, service::NetworkOperationStatus status) {
    service::NetworkResult result{};
    result.request_id = request_id;
    result.operation = service::NetworkOperationType::kConnect;
    result.status = status;
    return result;
}

}  // namespace

int main() {
    service::OperationResultStore store{};
    service::NetworkResult out{};
    service::ConfigResult config_out{};
    service::OtaResult ota_out{};
    service::RcpUpdateResult rcp_out{};

    const uint32_t first_request_id = store.next_request_id();
    const uint32_t second_request_id = store.next_request_id();
    assert(first_request_id != 0U);
    assert(second_request_id == first_request_id + 1U);

    store.note_network_poll_status(7U, service::NetworkOperationPollStatus::kScanQueued);
    assert(store.get_network_operation_poll_status(7U) == service::NetworkOperationPollStatus::kScanQueued);
    store.note_network_poll_status(7U, service::NetworkOperationPollStatus::kScanInProgress);
    assert(store.get_network_operation_poll_status(7U) == service::NetworkOperationPollStatus::kScanInProgress);
    assert(store.publish_network_result(make_result(7U, service::NetworkOperationStatus::kOk)));
    assert(store.publish_network_result(make_result(7U, service::NetworkOperationStatus::kHalFailed)));
    assert(store.get_network_operation_poll_status(7U) == service::NetworkOperationPollStatus::kReady);
    assert(store.take_network_result(7U, &out));
    assert(out.status == service::NetworkOperationStatus::kHalFailed);
    assert(store.get_network_operation_poll_status(7U) == service::NetworkOperationPollStatus::kNotReady);
    assert(!store.take_network_result(7U, &out));

    store.note_ota_poll_status(9U, service::OtaPollStatus::kQueued);
    assert(store.get_ota_poll_status(9U) == service::OtaPollStatus::kQueued);
    store.note_ota_poll_status(9U, service::OtaPollStatus::kDownloading);
    assert(store.get_ota_poll_status(9U) == service::OtaPollStatus::kDownloading);
    service::OtaResult ota_result{};
    ota_result.request_id = 9U;
    ota_result.status = service::OtaOperationStatus::kOk;
    ota_result.transport_socket_errno = 11;
    assert(store.publish_ota_result(ota_result));
    assert(store.get_ota_poll_status(9U) == service::OtaPollStatus::kReady);
    assert(store.take_ota_result(9U, &ota_out));
    assert(ota_out.status == service::OtaOperationStatus::kOk);
    assert(ota_out.transport_socket_errno == 11);
    assert(store.get_ota_poll_status(9U) == service::OtaPollStatus::kNotReady);
    assert(!store.take_ota_result(9U, &ota_out));

    store.note_rcp_update_poll_status(10U, service::RcpUpdatePollStatus::kQueued);
    assert(store.get_rcp_update_poll_status(10U) == service::RcpUpdatePollStatus::kQueued);
    store.note_rcp_update_poll_status(10U, service::RcpUpdatePollStatus::kApplying);
    assert(store.get_rcp_update_poll_status(10U) == service::RcpUpdatePollStatus::kApplying);
    service::RcpUpdateResult rcp_result{};
    rcp_result.request_id = 10U;
    rcp_result.status = service::RcpUpdateOperationStatus::kOk;
    rcp_result.written_bytes = 128U;
    assert(store.publish_rcp_update_result(rcp_result));
    assert(store.get_rcp_update_poll_status(10U) == service::RcpUpdatePollStatus::kReady);
    assert(store.take_rcp_update_result(10U, &rcp_out));
    assert(rcp_out.status == service::RcpUpdateOperationStatus::kOk);
    assert(rcp_out.written_bytes == 128U);
    assert(store.get_rcp_update_poll_status(10U) == service::RcpUpdatePollStatus::kNotReady);
    assert(!store.take_rcp_update_result(10U, &rcp_out));

    for (uint32_t request_id = 1U;
         request_id <= service::OperationResultStore::kNetworkResultQueueCapacity + 1U;
         ++request_id) {
        assert(store.publish_network_result(make_result(request_id, service::NetworkOperationStatus::kOk)));
    }

    assert(!store.take_network_result(1U, &out));
    assert(store.take_network_result(2U, &out));
    assert(out.request_id == 2U);

    service::ConfigResult config_result{};
    config_result.request_id = 100U;
    config_result.last_command_status = 1U;
    assert(store.publish_config_result(config_result));
    config_result.last_command_status = 2U;
    assert(store.publish_config_result(config_result));
    assert(store.pending_config_results() == 1U);
    assert(store.take_config_result(100U, &config_out));
    assert(config_out.last_command_status == 2U);
    assert(!store.take_config_result(100U, &config_out));

    return 0;
}
