/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstdint>

#include "reporting_manager.hpp"

namespace {

core::CoreEvent make_event(core::CoreEventType type, uint16_t short_addr) {
    core::CoreEvent event{};
    event.type = type;
    event.device_short_addr = short_addr;
    return event;
}

}  // namespace

int main() {
    service::ReportingManager manager{};

    (void)manager.handle_event(make_event(core::CoreEventType::kDeviceJoined, 0x2244));
    (void)manager.handle_event(make_event(core::CoreEventType::kDeviceInterviewCompleted, 0x2244));

    auto actions = manager.report_operation_failure(
        0x2244,
        service::ReportingManager::RetryTarget::kBind,
        service::ReportingManager::FailureReason::kTimeout,
        1000U);
    assert(!actions.request_bind);
    assert(!actions.request_configure_reporting);
    assert(!actions.mark_degraded);

    service::ReportingManager::RetryStatus retry{};
    assert(manager.get_retry_status(0x2244, &retry));
    assert(retry.target == service::ReportingManager::RetryTarget::kBind);
    assert(retry.reason == service::ReportingManager::FailureReason::kTimeout);
    assert(retry.attempt == 1U);
    assert(retry.pending);
    assert(retry.next_retry_at_ms == 2000U);

    actions = manager.process_timeouts(1999U);
    assert(!actions.request_bind);
    assert(!actions.request_configure_reporting);
    assert(!actions.mark_degraded);

    actions = manager.process_timeouts(2000U);
    assert(actions.request_bind);
    assert(!actions.request_configure_reporting);
    assert(!actions.mark_degraded);
    assert(manager.get_retry_status(0x2244, &retry));
    assert(!retry.pending);
    assert(retry.attempt == 1U);

    actions = manager.report_operation_failure(
        0x2244,
        service::ReportingManager::RetryTarget::kBind,
        service::ReportingManager::FailureReason::kNetworkError,
        3000U);
    assert(!actions.mark_degraded);
    assert(manager.get_retry_status(0x2244, &retry));
    assert(retry.attempt == 2U);
    assert(retry.pending);
    assert(retry.next_retry_at_ms == 5000U);

    actions = manager.report_operation_failure(
        0x2244,
        service::ReportingManager::RetryTarget::kBind,
        service::ReportingManager::FailureReason::kTimeout,
        5000U);
    assert(!actions.mark_degraded);
    assert(manager.get_retry_status(0x2244, &retry));
    assert(retry.attempt == 3U);
    assert(retry.pending);
    assert(retry.next_retry_at_ms == 9000U);

    actions = manager.report_operation_failure(
        0x2244,
        service::ReportingManager::RetryTarget::kBind,
        service::ReportingManager::FailureReason::kTimeout,
        9000U);
    assert(actions.mark_degraded);
    assert(manager.get_retry_status(0x2244, &retry));
    assert(!retry.pending);

    service::ReportingManager::State state{};
    assert(manager.get_state(0x2244, &state));
    assert(state == service::ReportingManager::State::kDegraded);

    (void)manager.handle_event(make_event(core::CoreEventType::kDeviceJoined, 0x3344));
    (void)manager.handle_event(make_event(core::CoreEventType::kDeviceInterviewCompleted, 0x3344));
    (void)manager.handle_event(make_event(core::CoreEventType::kDeviceBindingReady, 0x3344));
    actions = manager.report_operation_failure(
        0x3344,
        service::ReportingManager::RetryTarget::kConfigureReporting,
        service::ReportingManager::FailureReason::kUnsupportedAttr,
        100U);
    assert(actions.mark_degraded);
    assert(manager.get_state(0x3344, &state));
    assert(state == service::ReportingManager::State::kDegraded);
    assert(manager.get_retry_status(0x3344, &retry));
    assert(!retry.pending);
    assert(retry.reason == service::ReportingManager::FailureReason::kUnsupportedAttr);

    return 0;
}
