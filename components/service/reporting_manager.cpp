/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "reporting_manager.hpp"

#include <algorithm>

namespace service {

bool ReportingManager::valid_short_addr(uint16_t short_addr) noexcept {
    return short_addr != core::kUnknownDeviceShortAddr && short_addr != 0x0000U;
}

int ReportingManager::find_index(const std::array<Entry, core::kMaxDevices>& entries, uint16_t short_addr) noexcept {
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].short_addr == short_addr) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int ReportingManager::find_free_index(const std::array<Entry, core::kMaxDevices>& entries) noexcept {
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].short_addr == core::kUnknownDeviceShortAddr) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

ReportingManager::RuntimeActions ReportingManager::handle_event(const core::CoreEvent& event) noexcept {
    RuntimeActions actions{};
    if (!valid_short_addr(event.device_short_addr) && event.type != core::CoreEventType::kUnknown) {
        return actions;
    }

    int index = find_index(entries_, event.device_short_addr);
    if (index < 0 && event.type == core::CoreEventType::kDeviceJoined) {
        index = find_free_index(entries_);
        if (index >= 0) {
            entries_[static_cast<std::size_t>(index)].short_addr = event.device_short_addr;
            entries_[static_cast<std::size_t>(index)].state = State::kUnknown;
        }
    }

    if (index < 0) {
        return actions;
    }

    Entry& entry = entries_[static_cast<std::size_t>(index)];
    switch (event.type) {
        case core::CoreEventType::kDeviceJoined:
            if (entry.state != State::kPendingInterview) {
                entry.state = State::kPendingInterview;
                actions.request_interview = true;
            }
            break;

        case core::CoreEventType::kDeviceInterviewCompleted:
            if (entry.state != State::kPendingBind) {
                entry.state = State::kPendingBind;
                actions.request_bind = true;
            }
            break;

        case core::CoreEventType::kDeviceBindingReady:
            if (entry.state != State::kPendingConfigureReporting) {
                entry.state = State::kPendingConfigureReporting;
                actions.request_configure_reporting = true;
            }
            break;

        case core::CoreEventType::kDeviceReportingConfigured:
        case core::CoreEventType::kDeviceTelemetryUpdated:
            entry.state = State::kReportingActive;
            break;

        case core::CoreEventType::kDeviceStale:
            if (entry.state != State::kDegraded) {
                entry.state = State::kDegraded;
                actions.mark_degraded = true;
            }
            break;

        case core::CoreEventType::kDeviceLeft:
            entry = Entry{};
            break;

        case core::CoreEventType::kUnknown:
        case core::CoreEventType::kNetworkUp:
        case core::CoreEventType::kNetworkDown:
        case core::CoreEventType::kAttributeReported:
        case core::CoreEventType::kCommandSetDevicePowerRequested:
        case core::CoreEventType::kCommandRefreshNetworkRequested:
        case core::CoreEventType::kCommandResultSuccess:
        case core::CoreEventType::kCommandResultTimeout:
        case core::CoreEventType::kCommandResultFailed:
        default:
            break;
    }

    return actions;
}

bool ReportingManager::get_state(uint16_t short_addr, State* out) const noexcept {
    if (out == nullptr || !valid_short_addr(short_addr)) {
        return false;
    }

    const int index = find_index(entries_, short_addr);
    if (index < 0) {
        return false;
    }

    *out = entries_[static_cast<std::size_t>(index)].state;
    return true;
}

uint32_t ReportingManager::degraded_count() const noexcept {
    return static_cast<uint32_t>(std::count_if(
        entries_.begin(),
        entries_.end(),
        [](const Entry& entry) noexcept { return entry.state == State::kDegraded; }));
}

}  // namespace service
