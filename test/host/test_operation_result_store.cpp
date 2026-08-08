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

    // --- poll_operation_status(): unified domain-agnostic poll (plan S4
    // HTTP #1's GET /api/v1/operations/{operation_id}). ---
    {
        service::OperationStatusSnapshot unknown = store.poll_operation_status(999999U);
        assert(unknown.domain == service::OperationDomain::kUnknown);
        assert(unknown.status == service::OperationPollStatus::kUnknown);

        assert(store.poll_operation_status(0U).domain == service::OperationDomain::kUnknown);

        // Network: in-progress (scan) -> ready -> consumed -> unknown again.
        store.note_network_poll_status(501U, service::NetworkOperationPollStatus::kScanQueued);
        service::OperationStatusSnapshot net_in_progress = store.poll_operation_status(501U);
        assert(net_in_progress.domain == service::OperationDomain::kNetwork);
        assert(net_in_progress.status == service::OperationPollStatus::kInProgress);

        assert(store.publish_network_result(make_result(501U, service::NetworkOperationStatus::kOk)));
        service::OperationStatusSnapshot net_ready = store.poll_operation_status(501U);
        assert(net_ready.domain == service::OperationDomain::kNetwork);
        assert(net_ready.status == service::OperationPollStatus::kReady);

        service::NetworkResult consumed_network{};
        assert(store.take_network_result(501U, &consumed_network));
        assert(store.poll_operation_status(501U).domain == service::OperationDomain::kUnknown);

        // OTA: queued -> ready.
        store.note_ota_poll_status(502U, service::OtaPollStatus::kQueued);
        service::OperationStatusSnapshot ota_in_progress = store.poll_operation_status(502U);
        assert(ota_in_progress.domain == service::OperationDomain::kOta);
        assert(ota_in_progress.status == service::OperationPollStatus::kInProgress);

        service::OtaResult ota_result_for_poll{};
        ota_result_for_poll.request_id = 502U;
        ota_result_for_poll.status = service::OtaOperationStatus::kOk;
        assert(store.publish_ota_result(ota_result_for_poll));
        assert(store.poll_operation_status(502U).domain == service::OperationDomain::kOta);
        assert(store.poll_operation_status(502U).status == service::OperationPollStatus::kReady);

        // RCP update: applying -> ready.
        store.note_rcp_update_poll_status(503U, service::RcpUpdatePollStatus::kApplying);
        service::OperationStatusSnapshot rcp_in_progress = store.poll_operation_status(503U);
        assert(rcp_in_progress.domain == service::OperationDomain::kRcpUpdate);
        assert(rcp_in_progress.status == service::OperationPollStatus::kInProgress);

        service::RcpUpdateResult rcp_result_for_poll{};
        rcp_result_for_poll.request_id = 503U;
        rcp_result_for_poll.status = service::RcpUpdateOperationStatus::kOk;
        assert(store.publish_rcp_update_result(rcp_result_for_poll));
        assert(store.poll_operation_status(503U).domain == service::OperationDomain::kRcpUpdate);
        assert(store.poll_operation_status(503U).status == service::OperationPollStatus::kReady);

        // Config: no in-progress signal exists (documented limitation) --
        // only a published result is observable.
        assert(store.poll_operation_status(504U).domain == service::OperationDomain::kUnknown);
        service::ConfigResult config_for_poll{};
        config_for_poll.request_id = 504U;
        config_for_poll.last_command_status = 3U;
        assert(store.publish_config_result(config_for_poll));
        service::OperationStatusSnapshot config_ready = store.poll_operation_status(504U);
        assert(config_ready.domain == service::OperationDomain::kConfig);
        assert(config_ready.status == service::OperationPollStatus::kReady);
    }

    return 0;
}
