/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "core_state.hpp"

namespace core {

namespace {

int find_device_index(const CoreState& state, uint16_t short_addr) noexcept {
    for (std::size_t i = 0; i < state.devices.size(); ++i) {
        if (state.devices[i].short_addr == short_addr) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int find_free_slot(const CoreState& state) noexcept {
    for (std::size_t i = 0; i < state.devices.size(); ++i) {
        if (state.devices[i].short_addr == kUnknownDeviceShortAddr) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void push_state_persist_and_telemetry(CoreReduceResult* out) noexcept {
    // This helper is called before final revision increment in core_reduce.
    // Persist effect carries the upcoming revision that will be published.
    const uint32_t next_revision = out->next.revision + 1U;

    out->effects.push(CoreEffect{
        CoreEffectType::kPersistState,
        kNoCorrelationId,
        kUnknownDeviceShortAddr,
        next_revision,
        out->next.network_connected,
    });

    out->effects.push(CoreEffect{
        CoreEffectType::kPublishTelemetry,
        kNoCorrelationId,
        kUnknownDeviceShortAddr,
        out->next.device_count,
        out->next.network_connected,
    });
}

void apply_onoff_attribute(CoreReduceResult* out, const CoreEvent& event, bool* state_changed) noexcept {
    static constexpr uint16_t kOnOffClusterId = 0x0006;
    static constexpr uint16_t kOnOffAttributeId = 0x0000;

    if (event.cluster_id != kOnOffClusterId || event.attribute_id != kOnOffAttributeId) {
        return;
    }

    const int index = find_device_index(out->next, event.device_short_addr);
    if (index < 0) {
        return;
    }

    CoreDeviceRecord& device = out->next.devices[static_cast<std::size_t>(index)];
    if (device.power_on != event.value_bool || !device.online) {
        device.power_on = event.value_bool;
        device.online = true;
        *state_changed = true;
    }
}

bool apply_reporting_state(CoreReduceResult* out,
                           uint16_t short_addr,
                           CoreReportingState reporting_state,
                           bool stale) noexcept {
    if (short_addr == kUnknownDeviceShortAddr) {
        return false;
    }

    const int index = find_device_index(out->next, short_addr);
    if (index < 0) {
        return false;
    }

    CoreDeviceRecord& device = out->next.devices[static_cast<std::size_t>(index)];
    bool changed = false;
    if (device.reporting_state != reporting_state) {
        device.reporting_state = reporting_state;
        changed = true;
    }
    if (device.stale != stale) {
        device.stale = stale;
        changed = true;
    }
    return changed;
}

bool apply_telemetry_update(CoreReduceResult* out,
                            uint16_t short_addr,
                            uint32_t reported_at_ms) noexcept {
    if (short_addr == kUnknownDeviceShortAddr) {
        return false;
    }

    const int index = find_device_index(out->next, short_addr);
    if (index < 0) {
        return false;
    }

    CoreDeviceRecord& device = out->next.devices[static_cast<std::size_t>(index)];
    bool changed = false;
    if (device.reporting_state != CoreReportingState::kReportingActive) {
        device.reporting_state = CoreReportingState::kReportingActive;
        changed = true;
    }
    if (device.stale) {
        device.stale = false;
        changed = true;
    }
    if (device.last_report_at_ms != reported_at_ms) {
        device.last_report_at_ms = reported_at_ms;
        changed = true;
    }

    return changed;
}

}  // namespace

CoreReduceResult core_reduce(const CoreState& prev, const CoreEvent& event) noexcept {
    CoreReduceResult out{};
    out.next = prev;

    bool state_changed = false;

    switch (event.type) {
        case CoreEventType::kDeviceJoined: {
            if (event.device_short_addr == kUnknownDeviceShortAddr) {
                break;
            }

            int index = find_device_index(out.next, event.device_short_addr);
            if (index < 0) {
                index = find_free_slot(out.next);
                if (index >= 0) {
                    CoreDeviceRecord& device = out.next.devices[static_cast<std::size_t>(index)];
                    device.short_addr = event.device_short_addr;
                    device.online = true;
                    device.power_on = false;
                    ++out.next.device_count;
                    state_changed = true;
                }
            } else {
                CoreDeviceRecord& device = out.next.devices[static_cast<std::size_t>(index)];
                if (!device.online) {
                    device.short_addr = event.device_short_addr;
                    device.online = true;
                    ++out.next.device_count;
                    state_changed = true;
                }
            }

            if (state_changed) {
                push_state_persist_and_telemetry(&out);
            }
            break;
        }

        case CoreEventType::kDeviceLeft: {
            if (event.device_short_addr == kUnknownDeviceShortAddr) {
                break;
            }

            const int index = find_device_index(out.next, event.device_short_addr);
            if (index >= 0) {
                CoreDeviceRecord& device = out.next.devices[static_cast<std::size_t>(index)];
                if (device.online && out.next.device_count > 0) {
                    --out.next.device_count;
                }
                device = CoreDeviceRecord{};
                state_changed = true;
            }

            if (state_changed) {
                push_state_persist_and_telemetry(&out);
            }
            break;
        }

        case CoreEventType::kNetworkUp:
            if (!out.next.network_connected) {
                out.next.network_connected = true;
                state_changed = true;
                push_state_persist_and_telemetry(&out);
            }
            break;

        case CoreEventType::kNetworkDown:
            if (out.next.network_connected) {
                out.next.network_connected = false;
                state_changed = true;
                push_state_persist_and_telemetry(&out);
            }
            break;

        case CoreEventType::kAttributeReported:
            apply_onoff_attribute(&out, event, &state_changed);
            if (state_changed) {
                push_state_persist_and_telemetry(&out);
            }
            break;

        case CoreEventType::kCommandSetDevicePowerRequested:
            out.effects.push(CoreEffect{
                CoreEffectType::kSendZigbeeOnOff,
                event.correlation_id,
                event.device_short_addr,
                0,
                event.value_bool,
            });
            break;

        case CoreEventType::kCommandRefreshNetworkRequested:
            out.effects.push(CoreEffect{
                CoreEffectType::kRefreshNetwork,
                event.correlation_id,
                kUnknownDeviceShortAddr,
                0,
                false,
            });
            break;

        case CoreEventType::kCommandResultSuccess:
            out.next.last_command_status = 1;
            state_changed = true;
            out.effects.push(CoreEffect{
                CoreEffectType::kEmitCommandResult,
                event.correlation_id,
                event.device_short_addr,
                1,
                true,
            });
            break;

        case CoreEventType::kCommandResultTimeout:
            out.next.last_command_status = 2;
            state_changed = true;
            out.effects.push(CoreEffect{
                CoreEffectType::kEmitCommandResult,
                event.correlation_id,
                event.device_short_addr,
                2,
                false,
            });
            break;

        case CoreEventType::kCommandResultFailed:
            out.next.last_command_status = 3;
            state_changed = true;
            out.effects.push(CoreEffect{
                CoreEffectType::kEmitCommandResult,
                event.correlation_id,
                event.device_short_addr,
                3,
                false,
            });
            break;

        case CoreEventType::kDeviceInterviewCompleted:
            state_changed = apply_reporting_state(
                &out,
                event.device_short_addr,
                CoreReportingState::kInterviewCompleted,
                false);
            if (state_changed) {
                push_state_persist_and_telemetry(&out);
            }
            break;

        case CoreEventType::kDeviceBindingReady:
            state_changed = apply_reporting_state(
                &out,
                event.device_short_addr,
                CoreReportingState::kBindingReady,
                false);
            if (state_changed) {
                push_state_persist_and_telemetry(&out);
            }
            break;

        case CoreEventType::kDeviceReportingConfigured:
            state_changed = apply_reporting_state(
                &out,
                event.device_short_addr,
                CoreReportingState::kReportingConfigured,
                false);
            if (state_changed) {
                push_state_persist_and_telemetry(&out);
            }
            break;

        case CoreEventType::kDeviceTelemetryUpdated:
            state_changed = apply_telemetry_update(&out, event.device_short_addr, event.value_u32);
            if (state_changed) {
                push_state_persist_and_telemetry(&out);
            }
            break;

        case CoreEventType::kDeviceStale:
            state_changed = apply_reporting_state(
                &out,
                event.device_short_addr,
                CoreReportingState::kStale,
                true);
            if (state_changed) {
                push_state_persist_and_telemetry(&out);
            }
            break;

        case CoreEventType::kUnknown:
        default:
            break;
    }

    if (state_changed) {
        out.next.revision = prev.revision + 1U;
    }

    return out;
}

}  // namespace core
