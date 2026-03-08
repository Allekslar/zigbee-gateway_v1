/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "matter_bridge.hpp"

#include <algorithm>
#include <cstring>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal_matter.h"
#endif
#include "log_tags.h"

namespace matter_bridge {
namespace {

#ifdef ESP_PLATFORM
constexpr const char* kTag = LOG_TAG_MATTER_BRIDGE;
constexpr const char* kMatterBridgeTaskName = "matter_bridge";
constexpr uint32_t kMatterBridgeTaskStackSize = 6144U;
constexpr UBaseType_t kMatterBridgeTaskPriority = 4U;
constexpr TickType_t kMatterBridgeTaskPeriodTicks = pdMS_TO_TICKS(1000);
#endif

bool is_active_device(const service::MatterBridgeDeviceSnapshot& device) noexcept {
    return device.short_addr != core::kUnknownDeviceShortAddr && device.online;
}

MatterDeviceClass infer_primary_class(const service::MatterBridgeDeviceSnapshot& device) noexcept {
    switch (device.primary_class) {
        case service::MatterBridgeDeviceClass::kTemperature:
            return MatterDeviceClass::kTemperature;
        case service::MatterBridgeDeviceClass::kOccupancy:
            return MatterDeviceClass::kOccupancy;
        case service::MatterBridgeDeviceClass::kContact:
            return MatterDeviceClass::kContact;
        case service::MatterBridgeDeviceClass::kUnknown:
        default:
            return MatterDeviceClass::kUnknown;
    }
}

uint16_t resolve_status_endpoint(const MatterEndpointMapEntry* map,
                                 std::size_t map_size,
                                 const service::MatterBridgeDeviceSnapshot& device) noexcept {
    uint16_t endpoint = 0;
    (void)map_resolve_endpoint(map, map_size, device.short_addr, infer_primary_class(device), &endpoint);
    return endpoint;
}

bool publish_update_to_hal(const MatterAttributeUpdate& update) noexcept {
#ifdef ESP_PLATFORM
    hal_matter_attribute_update_t hal_update{};
    hal_update.endpoint_id = update.endpoint;
    hal_update.bool_value = update.bool_value;
    hal_update.int_value = update.int_value;

    switch (update.type) {
        case MatterAttributeType::kAvailabilityOnline:
            hal_update.attr_type = HAL_MATTER_ATTR_AVAILABILITY_ONLINE;
            break;
        case MatterAttributeType::kTemperatureCentiC:
            hal_update.attr_type = HAL_MATTER_ATTR_TEMPERATURE_CENTI_C;
            break;
        case MatterAttributeType::kOccupancy:
            hal_update.attr_type = HAL_MATTER_ATTR_OCCUPANCY;
            break;
        case MatterAttributeType::kContactOpen:
            hal_update.attr_type = HAL_MATTER_ATTR_CONTACT_OPEN;
            break;
        case MatterAttributeType::kStale:
            hal_update.attr_type = HAL_MATTER_ATTR_STALE;
            break;
    }

    return hal_matter_publish_attribute_update(&hal_update) == 0;
#else
    (void)update;
    return true;
#endif
}

}  // namespace

bool MatterBridge::start() noexcept {
    reset_sync_state();
#ifdef ESP_PLATFORM
    if (hal_matter_init() != 0) {
        ESP_LOGW(kTag, "Matter HAL init unavailable; bridge will run without platform publisher");
    }
#endif
    started_.store(true, std::memory_order_release);
#ifdef ESP_PLATFORM
    return ensure_task_started();
#else
    return started();
#endif
}

void MatterBridge::stop() noexcept {
    reset_sync_state();
    started_.store(false, std::memory_order_release);
#ifdef ESP_PLATFORM
    for (uint8_t i = 0; i < 20U && task_handle_ != nullptr; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
#endif
}

bool MatterBridge::started() const noexcept {
    return started_.load(std::memory_order_acquire);
}

void MatterBridge::attach_runtime(service::ServiceRuntimeApi* runtime) noexcept {
    runtime_ = runtime;
#ifdef ESP_PLATFORM
    (void)ensure_task_started();
#endif
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

std::size_t MatterBridge::sync_runtime_snapshot() noexcept {
    if (!started() || runtime_ == nullptr) {
        return 0U;
    }

    if (!runtime_->build_matter_bridge_snapshot(&runtime_snapshot_cache_)) {
        return 0U;
    }

    return sync_snapshot(runtime_snapshot_cache_);
}

std::size_t MatterBridge::sync_snapshot(const service::MatterBridgeSnapshot& snapshot) noexcept {
    if (!started()) {
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

    std::size_t next_count = 0;

    for (std::size_t i = 0; i < snapshot.device_count && next_count < core::kMaxDevices; ++i) {
        const service::MatterBridgeDeviceSnapshot& device = snapshot.devices[i];
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
        next.has_occupancy = device.has_occupancy;
        next.occupied = device.occupied;
        next.has_contact = device.has_contact;
        next.contact_open = device.contact_open;
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

        if (next.has_occupancy && (is_new || prev->has_occupancy != next.has_occupancy || prev->occupied != next.occupied)) {
            uint16_t endpoint = 0;
            if (map_resolve_endpoint(endpoint_map_, endpoint_map_size_, next.short_addr, MatterDeviceClass::kOccupancy, &endpoint) &&
                endpoint != 0U) {
                MatterAttributeUpdate update{};
                update.short_addr = next.short_addr;
                update.endpoint = endpoint;
                update.type = MatterAttributeType::kOccupancy;
                update.bool_value = next.occupied;
                enqueue(update);
            }
        }

        if (next.has_contact && (is_new || prev->has_contact != next.has_contact || prev->contact_open != next.contact_open)) {
            uint16_t endpoint = 0;
            if (map_resolve_endpoint(endpoint_map_, endpoint_map_size_, next.short_addr, MatterDeviceClass::kContact, &endpoint) &&
                endpoint != 0U) {
                MatterAttributeUpdate update{};
                update.short_addr = next.short_addr;
                update.endpoint = endpoint;
                update.type = MatterAttributeType::kContactOpen;
                update.bool_value = next.contact_open;
                enqueue(update);
            }
        }

        sync_shadow_scratch_[next_count++] = next;
    }

    for (std::size_t i = 0; i < core::kMaxDevices; ++i) {
        const DeviceShadow& prev = cached_devices_[i];
        if (!prev.in_use) {
            continue;
        }
        if (find_shadow(sync_shadow_scratch_, next_count, prev.short_addr) != nullptr) {
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
        cached_devices_[i] = sync_shadow_scratch_[i];
    }

    // Keep transport side effects out of Core by publishing normalized updates via HAL C ABI.
    for (std::size_t i = 0; i < pending_update_count_; ++i) {
        (void)publish_update_to_hal(pending_updates_[i]);
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
        sync_shadow_scratch_[i] = DeviceShadow{};
    }
}

#ifdef ESP_PLATFORM
void MatterBridge::task_entry(void* arg) noexcept {
    auto* bridge = static_cast<MatterBridge*>(arg);
    if (bridge != nullptr) {
        bridge->run_loop();
    }
    vTaskDelete(nullptr);
}

void MatterBridge::run_loop() noexcept {
    while (started()) {
        (void)sync_runtime_snapshot();
        vTaskDelay(kMatterBridgeTaskPeriodTicks);
    }
    task_handle_ = nullptr;
}

bool MatterBridge::ensure_task_started() noexcept {
    if (!started()) {
        return false;
    }
    if (task_handle_ != nullptr) {
        return true;
    }
    if (runtime_ == nullptr) {
        return true;
    }

    TaskHandle_t handle = nullptr;
    const BaseType_t created = xTaskCreate(
        &MatterBridge::task_entry,
        kMatterBridgeTaskName,
        kMatterBridgeTaskStackSize,
        this,
        kMatterBridgeTaskPriority,
        &handle);
    if (created != pdPASS) {
        ESP_LOGE(kTag, "Matter bridge task start failed");
        return false;
    }

    task_handle_ = handle;
    return true;
}
#endif

}  // namespace matter_bridge
