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

    assert(store.publish_network_result(make_result(7U, service::NetworkOperationStatus::kOk)));
    assert(store.publish_network_result(make_result(7U, service::NetworkOperationStatus::kHalFailed)));
    assert(store.take_network_result(7U, &out));
    assert(out.status == service::NetworkOperationStatus::kHalFailed);
    assert(!store.take_network_result(7U, &out));

    for (uint32_t request_id = 1U;
         request_id <= service::OperationResultStore::kNetworkResultQueueCapacity + 1U;
         ++request_id) {
        assert(store.publish_network_result(make_result(request_id, service::NetworkOperationStatus::kOk)));
    }

    assert(!store.take_network_result(1U, &out));
    assert(store.take_network_result(2U, &out));
    assert(out.request_id == 2U);
    return 0;
}
