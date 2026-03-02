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

    service::ReportingManager::State state{};
    assert(!manager.get_state(0x1234, &state));

    const auto joined_actions = manager.handle_event(make_event(core::CoreEventType::kDeviceJoined, 0x1234));
    assert(joined_actions.request_interview);
    assert(!joined_actions.request_bind);
    assert(!joined_actions.request_configure_reporting);
    assert(!joined_actions.mark_degraded);
    assert(manager.get_state(0x1234, &state));
    assert(state == service::ReportingManager::State::kPendingInterview);

    const auto joined_repeat = manager.handle_event(make_event(core::CoreEventType::kDeviceJoined, 0x1234));
    assert(!joined_repeat.request_interview);
    assert(manager.get_state(0x1234, &state));
    assert(state == service::ReportingManager::State::kPendingInterview);

    const auto interview_actions =
        manager.handle_event(make_event(core::CoreEventType::kDeviceInterviewCompleted, 0x1234));
    assert(interview_actions.request_bind);
    assert(manager.get_state(0x1234, &state));
    assert(state == service::ReportingManager::State::kPendingBind);

    const auto bind_actions = manager.handle_event(make_event(core::CoreEventType::kDeviceBindingReady, 0x1234));
    assert(bind_actions.request_configure_reporting);
    assert(manager.get_state(0x1234, &state));
    assert(state == service::ReportingManager::State::kPendingConfigureReporting);

    const auto configured_actions =
        manager.handle_event(make_event(core::CoreEventType::kDeviceReportingConfigured, 0x1234));
    assert(!configured_actions.request_interview);
    assert(!configured_actions.request_bind);
    assert(!configured_actions.request_configure_reporting);
    assert(!configured_actions.mark_degraded);
    assert(manager.get_state(0x1234, &state));
    assert(state == service::ReportingManager::State::kReportingActive);

    const auto stale_actions = manager.handle_event(make_event(core::CoreEventType::kDeviceStale, 0x1234));
    assert(stale_actions.mark_degraded);
    assert(manager.get_state(0x1234, &state));
    assert(state == service::ReportingManager::State::kDegraded);

    const auto telemetry_actions =
        manager.handle_event(make_event(core::CoreEventType::kDeviceTelemetryUpdated, 0x1234));
    assert(!telemetry_actions.request_interview);
    assert(!telemetry_actions.request_bind);
    assert(!telemetry_actions.request_configure_reporting);
    assert(!telemetry_actions.mark_degraded);
    assert(manager.get_state(0x1234, &state));
    assert(state == service::ReportingManager::State::kReportingActive);

    (void)manager.handle_event(make_event(core::CoreEventType::kDeviceLeft, 0x1234));
    assert(!manager.get_state(0x1234, &state));

    return 0;
}
