/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "matter_bridge.hpp"

#include <algorithm>
#include <cstring>

namespace matter_bridge {
namespace {

bool is_active_device(const core::CoreDeviceRecord& device) noexcept {
    return device.short_addr != core::kUnknownDeviceShortAddr && device.online;
}

MatterDeviceClass infer_primary_class(const core::CoreDeviceRecord& device) noexcept {
    if (device.has_temperature) {
        return MatterDeviceClass::kTemperature;
    }
    if (device.occupancy_state != core::CoreOccupancyState::kUnknown) {
        return MatterDeviceClass::kOccupancy;
    }
    if (device.contact_state != core::CoreContactState::kUnknown) {
        return MatterDeviceClass::kContact;
    }
    return MatterDeviceClass::kTemperature;
}

uint16_t resolve_status_endpoint(const MatterEndpointMapEntry* map,
                                 std::size_t map_size,
                                 const core::CoreDeviceRecord& device) noexcept {
    uint16_t endpoint = 0;
    (void)map_resolve_endpoint(map, map_size, device.short_addr, infer_primary_class(device), &endpoint);
    return endpoint;
}

}  // namespace

bool MatterBridge::start() noexcept {
    reset_sync_state();
    started_ = true;
    return started_;
}

void MatterBridge::stop() noexcept {
    reset_sync_state();
    started_ = false;
}

bool MatterBridge::started() const noexcept {
    return started_;
}

bool MatterBridge::set_endpoint_map(const MatterEndpointMapEntry* map, std::size_t size) noexcept {
    if (size > kMatterMaxEndpointMapEntries) {
        return false;
    }
    if (size > 0U && map == nullptr) {
        return false;
    }

    if (size == 0U) {
        endpoint_map_size_ = 0U;
        return true;
    }

    std::memcpy(endpoint_map_, map, sizeof(MatterEndpointMapEntry) * size);
    endpoint_map_size_ = size;
    return true;
}

std::size_t MatterBridge::sync_snapshot(const core::CoreState& state) noexcept {
    if (!started_) {
        return 0U;
    }

    pending_update_count_ = 0;

    auto enqueue = [&](const MatterAttributeUpdate& update) noexcept {
        if (pending_update_count_ < kMatterMaxUpdatesPerSync) {
            pending_updates_[pending_update_count_++] = update;
        }
    };

    auto find_shadow = [](const DeviceShadow* shadows,
                          std::size_t size,
                          uint16_t short_addr) noexcept -> const DeviceShadow* {
        for (std::size_t i = 0; i < size; ++i) {
            if (shadows[i].in_use && shadows[i].short_addr == short_addr) {
                return &shadows[i];
            }
        }
        return nullptr;
    };

    DeviceShadow next_cache[core::kMaxDevices]{};
    std::size_t next_count = 0;

    for (std::size_t i = 0; i < state.devices.size() && next_count < core::kMaxDevices; ++i) {
        const core::CoreDeviceRecord& device = state.devices[i];
        if (!is_active_device(device)) {
            continue;
        }

        DeviceShadow next{};
        next.in_use = true;
        next.short_addr = device.short_addr;
        next.online = true;
        next.stale = device.stale;
        next.has_temperature = device.has_temperature;
        next.temperature_centi_c = device.temperature_centi_c;
        next.occupancy = device.occupancy_state;
        next.contact = device.contact_state;
        next.status_endpoint = resolve_status_endpoint(endpoint_map_, endpoint_map_size_, device);

        const DeviceShadow* prev = find_shadow(cached_devices_, core::kMaxDevices, next.short_addr);
        const bool is_new = (prev == nullptr);

        if (next.status_endpoint != 0U && (is_new || !prev->online)) {
            MatterAttributeUpdate update{};
            update.short_addr = next.short_addr;
            update.endpoint = next.status_endpoint;
            update.type = MatterAttributeType::kAvailabilityOnline;
            update.bool_value = true;
            enqueue(update);
        }

        if (next.status_endpoint != 0U && (is_new || prev->stale != next.stale)) {
            MatterAttributeUpdate update{};
            update.short_addr = next.short_addr;
            update.endpoint = next.status_endpoint;
            update.type = MatterAttributeType::kStale;
            update.bool_value = next.stale;
            enqueue(update);
        }

        if (next.has_temperature && (is_new || !prev->has_temperature || prev->temperature_centi_c != next.temperature_centi_c)) {
            uint16_t endpoint = 0;
            if (map_resolve_endpoint(endpoint_map_, endpoint_map_size_, next.short_addr, MatterDeviceClass::kTemperature, &endpoint) &&
                endpoint != 0U) {
                MatterAttributeUpdate update{};
                update.short_addr = next.short_addr;
                update.endpoint = endpoint;
                update.type = MatterAttributeType::kTemperatureCentiC;
                update.int_value = static_cast<int32_t>(next.temperature_centi_c);
                enqueue(update);
            }
        }

        if (next.occupancy != core::CoreOccupancyState::kUnknown && (is_new || prev->occupancy != next.occupancy)) {
            uint16_t endpoint = 0;
            if (map_resolve_endpoint(endpoint_map_, endpoint_map_size_, next.short_addr, MatterDeviceClass::kOccupancy, &endpoint) &&
                endpoint != 0U) {
                MatterAttributeUpdate update{};
                update.short_addr = next.short_addr;
                update.endpoint = endpoint;
                update.type = MatterAttributeType::kOccupancy;
                update.bool_value = (next.occupancy == core::CoreOccupancyState::kOccupied);
                enqueue(update);
            }
        }

        if (next.contact != core::CoreContactState::kUnknown && (is_new || prev->contact != next.contact)) {
            uint16_t endpoint = 0;
            if (map_resolve_endpoint(endpoint_map_, endpoint_map_size_, next.short_addr, MatterDeviceClass::kContact, &endpoint) &&
                endpoint != 0U) {
                MatterAttributeUpdate update{};
                update.short_addr = next.short_addr;
                update.endpoint = endpoint;
                update.type = MatterAttributeType::kContactOpen;
                update.bool_value = (next.contact == core::CoreContactState::kOpen);
                enqueue(update);
            }
        }

        next_cache[next_count++] = next;
    }

    for (std::size_t i = 0; i < core::kMaxDevices; ++i) {
        const DeviceShadow& prev = cached_devices_[i];
        if (!prev.in_use) {
            continue;
        }
        if (find_shadow(next_cache, next_count, prev.short_addr) != nullptr) {
            continue;
        }
        if (prev.status_endpoint == 0U) {
            continue;
        }

        MatterAttributeUpdate update{};
        update.short_addr = prev.short_addr;
        update.endpoint = prev.status_endpoint;
        update.type = MatterAttributeType::kAvailabilityOnline;
        update.bool_value = false;
        enqueue(update);
    }

    for (std::size_t i = 0; i < core::kMaxDevices; ++i) {
        cached_devices_[i] = DeviceShadow{};
    }
    for (std::size_t i = 0; i < next_count; ++i) {
        cached_devices_[i] = next_cache[i];
    }

    return pending_update_count_;
}

std::size_t MatterBridge::drain_attribute_updates(MatterAttributeUpdate* out, std::size_t capacity) noexcept {
    if (out == nullptr || capacity == 0U || pending_update_count_ == 0U) {
        return 0U;
    }

    const std::size_t to_copy = std::min(capacity, pending_update_count_);
    for (std::size_t i = 0; i < to_copy; ++i) {
        out[i] = pending_updates_[i];
    }

    const std::size_t remaining = pending_update_count_ - to_copy;
    for (std::size_t i = 0; i < remaining; ++i) {
        pending_updates_[i] = pending_updates_[i + to_copy];
    }
    pending_update_count_ = remaining;

    return to_copy;
}

void MatterBridge::reset_sync_state() noexcept {
    pending_update_count_ = 0U;
    for (std::size_t i = 0; i < core::kMaxDevices; ++i) {
        cached_devices_[i] = DeviceShadow{};
    }
}

}  // namespace matter_bridge
