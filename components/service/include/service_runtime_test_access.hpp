/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#if !defined(SERVICE_RUNTIME_TEST_HOOKS)
#error "service_runtime_test_access.hpp is test-only and requires SERVICE_RUNTIME_TEST_HOOKS"
#endif

#include <cstdint>

#include "service_runtime.hpp"

namespace service {

class ServiceRuntimeTestAccess {
public:
    static bool pop_scan_worker_request(ServiceRuntime& runtime, uint32_t* request_id) noexcept {
        return runtime.scan_manager_.pop_request_for_test(request_id);
    }

    static void set_scan_request_in_progress(ServiceRuntime& runtime, uint32_t request_id) noexcept {
        runtime.scan_manager_.set_request_in_progress_for_test(request_id);
    }

    static void clear_scan_request_in_progress(ServiceRuntime& runtime) noexcept {
        runtime.scan_manager_.clear_request_in_progress_for_test();
    }

    static bool push_network_result(ServiceRuntime& runtime, const ServiceRuntime::NetworkResult& result) noexcept {
        return runtime.queue_network_result(result);
    }

    static uint32_t monotonic_now_ms(const ServiceRuntime& runtime) noexcept {
        return runtime.monotonic_now_ms();
    }
};

}  // namespace service
