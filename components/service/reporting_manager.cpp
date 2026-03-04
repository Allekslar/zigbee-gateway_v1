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

bool ReportingManager::is_retryable_reason(FailureReason reason) noexcept {
    switch (reason) {
        case FailureReason::kTimeout:
        case FailureReason::kNetworkError:
            return true;
        case FailureReason::kNone:
        case FailureReason::kNotSupported:
        case FailureReason::kUnsupportedAttr:
        default:
            return false;
    }
}

uint32_t ReportingManager::compute_backoff_ms(uint8_t attempt) noexcept {
    if (attempt == 0U) {
        return kBaseBackoffMs;
    }

    uint32_t delay = kBaseBackoffMs;
    for (uint8_t i = 1U; i < attempt; ++i) {
        if (delay >= (kMaxBackoffMs / 2U)) {
            delay = kMaxBackoffMs;
            break;
        }
        delay *= 2U;
    }
    if (delay > kMaxBackoffMs) {
        delay = kMaxBackoffMs;
    }
    return delay;
}

bool ReportingManager::is_due(uint32_t now_ms, uint32_t deadline_ms) noexcept {
    return static_cast<uint32_t>(now_ms - deadline_ms) < 0x80000000U;
}

ConfigManager::ReportingDeviceClass ReportingManager::classify_device_class(uint16_t cluster_id) noexcept {
    switch (cluster_id) {
        case 0x0402U:
            return ConfigManager::ReportingDeviceClass::kTemperature;
        case 0x0406U:
            return ConfigManager::ReportingDeviceClass::kMotion;
        case 0x0500U:
            return ConfigManager::ReportingDeviceClass::kContact;
        default:
            return ConfigManager::ReportingDeviceClass::kUnknown;
    }
}

void ReportingManager::clear_retry_state(Entry* entry) noexcept {
    if (entry == nullptr) {
        return;
    }

    entry->retry_target = RetryTarget::kNone;
    entry->last_failure_reason = FailureReason::kNone;
    entry->retry_attempt = 0U;
    entry->next_retry_at_ms = 0U;
    entry->retry_pending = false;
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
            entry.stale_pending = false;
            break;

        case core::CoreEventType::kDeviceInterviewCompleted:
            clear_retry_state(&entry);
            if (entry.state != State::kPendingBind) {
                entry.state = State::kPendingBind;
                actions.request_bind = true;
            }
            break;

        case core::CoreEventType::kDeviceBindingReady:
            clear_retry_state(&entry);
            if (entry.state != State::kPendingConfigureReporting) {
                entry.state = State::kPendingConfigureReporting;
                actions.request_configure_reporting = true;
            }
            break;

        case core::CoreEventType::kDeviceReportingConfigured:
            clear_retry_state(&entry);
            entry.state = State::kReportingActive;
            entry.stale_pending = false;
            break;

        case core::CoreEventType::kDeviceTelemetryUpdated:
            clear_retry_state(&entry);
            entry.state = State::kReportingActive;
            entry.stale_pending = false;
            if (event.value_u32 != 0U) {
                entry.last_report_at_ms = event.value_u32;
            }
            break;

        case core::CoreEventType::kDeviceStale:
            if (entry.state != State::kDegraded) {
                entry.state = State::kDegraded;
                actions.mark_degraded = true;
            }
            clear_retry_state(&entry);
            entry.stale_pending = false;
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

ReportingManager::RuntimeActions ReportingManager::report_operation_failure(
    uint16_t short_addr,
    RetryTarget target,
    FailureReason reason,
    uint32_t now_ms) noexcept {
    RuntimeActions actions{};
    if (!valid_short_addr(short_addr) || target == RetryTarget::kNone || reason == FailureReason::kNone) {
        return actions;
    }

    const int index = find_index(entries_, short_addr);
    if (index < 0) {
        return actions;
    }

    Entry& entry = entries_[static_cast<std::size_t>(index)];
    entry.retry_target = target;
    entry.last_failure_reason = reason;

    if (!is_retryable_reason(reason)) {
        entry.state = State::kDegraded;
        entry.retry_pending = false;
        actions.mark_degraded = true;
        return actions;
    }

    if (entry.retry_attempt >= kMaxRetryAttempts) {
        entry.state = State::kDegraded;
        entry.retry_pending = false;
        actions.mark_degraded = true;
        return actions;
    }

    ++entry.retry_attempt;
    entry.retry_pending = true;
    entry.next_retry_at_ms = now_ms + compute_backoff_ms(entry.retry_attempt);
    return actions;
}

ReportingManager::RuntimeActions ReportingManager::process_timeouts(uint32_t now_ms) noexcept {
    RuntimeActions actions{};
    for (Entry& entry : entries_) {
        if (entry.short_addr == core::kUnknownDeviceShortAddr || !entry.retry_pending) {
            continue;
        }
        if (!is_due(now_ms, entry.next_retry_at_ms)) {
            continue;
        }

        entry.retry_pending = false;
        switch (entry.retry_target) {
            case RetryTarget::kBind:
                actions.request_bind = true;
                entry.state = State::kPendingBind;
                break;
            case RetryTarget::kConfigureReporting:
                actions.request_configure_reporting = true;
                entry.state = State::kPendingConfigureReporting;
                break;
            case RetryTarget::kNone:
            default:
                break;
        }
    }

    return actions;
}

std::size_t ReportingManager::collect_stale_candidates(
    uint32_t now_ms,
    uint32_t max_silence_window_ms,
    std::array<uint16_t, core::kMaxDevices>* out_short_addrs) noexcept {
    if (out_short_addrs == nullptr || max_silence_window_ms == 0U) {
        return 0;
    }

    std::size_t count = 0;
    for (Entry& entry : entries_) {
        if (entry.short_addr == core::kUnknownDeviceShortAddr) {
            continue;
        }
        if (entry.state != State::kReportingActive || entry.last_report_at_ms == 0U || entry.stale_pending) {
            continue;
        }

        const uint32_t stale_deadline = entry.last_report_at_ms + max_silence_window_ms;
        if (!is_due(now_ms, stale_deadline)) {
            continue;
        }
        if (count >= out_short_addrs->size()) {
            break;
        }

        (*out_short_addrs)[count++] = entry.short_addr;
        entry.stale_pending = true;
    }

    return count;
}

bool ReportingManager::set_stale_pending(uint16_t short_addr, bool pending) noexcept {
    if (!valid_short_addr(short_addr)) {
        return false;
    }

    const int index = find_index(entries_, short_addr);
    if (index < 0) {
        return false;
    }

    entries_[static_cast<std::size_t>(index)].stale_pending = pending;
    return true;
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

bool ReportingManager::get_retry_status(uint16_t short_addr, RetryStatus* out) const noexcept {
    if (out == nullptr || !valid_short_addr(short_addr)) {
        return false;
    }

    const int index = find_index(entries_, short_addr);
    if (index < 0) {
        return false;
    }

    const Entry& entry = entries_[static_cast<std::size_t>(index)];
    out->target = entry.retry_target;
    out->reason = entry.last_failure_reason;
    out->attempt = entry.retry_attempt;
    out->next_retry_at_ms = entry.next_retry_at_ms;
    out->pending = entry.retry_pending;
    return true;
}

uint32_t ReportingManager::degraded_count() const noexcept {
    return static_cast<uint32_t>(std::count_if(
        entries_.begin(),
        entries_.end(),
        [](const Entry& entry) noexcept { return entry.state == State::kDegraded; }));
}

bool ReportingManager::resolve_profile_for_device(
    const ConfigManager& config,
    uint16_t short_addr,
    uint8_t endpoint,
    uint16_t cluster_id,
    ConfigManager::ReportingProfile* out) const noexcept {
    if (out == nullptr || !valid_short_addr(short_addr) || endpoint == 0U || cluster_id == 0U) {
        return false;
    }

    ConfigManager::ReportingProfileKey key{};
    key.short_addr = short_addr;
    key.endpoint = endpoint;
    key.cluster_id = cluster_id;
    return config.resolve_reporting_profile(key, classify_device_class(cluster_id), out);
}

}  // namespace service
