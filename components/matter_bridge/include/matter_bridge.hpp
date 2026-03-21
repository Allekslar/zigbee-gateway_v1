/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "matter_endpoint_map.hpp"
#include "matter_runtime_api.hpp"

namespace matter_bridge {

enum class MatterAttributeType : uint8_t {
    kAvailabilityOnline = 0,
    kTemperatureCentiC,
    kOccupancy,
    kContactOpen,
    kStale,
};

struct MatterAttributeUpdate {
    uint16_t short_addr{0};
    uint16_t endpoint{0};
    MatterAttributeType type{MatterAttributeType::kAvailabilityOnline};
    bool bool_value{false};
    int32_t int_value{0};
};

constexpr std::size_t kMatterMaxEndpointMapEntries = service::kServiceMaxDevices;
constexpr std::size_t kMatterMaxUpdatesPerSync = service::kServiceMaxDevices * 5U;

class MatterBridge {
public:
    bool start() noexcept;
    void stop() noexcept;
    bool started() const noexcept;
    void attach_runtime(service::MatterRuntimeApi* runtime) noexcept;
    bool set_endpoint_map(const MatterEndpointMapEntry* map, std::size_t size) noexcept;
    service::CommandSubmitStatus post_power_command(
        uint16_t short_addr,
        bool desired_power_on,
        uint32_t issued_at_ms,
        uint32_t* correlation_id_out) noexcept;
    std::size_t sync_runtime_snapshot() noexcept;
    std::size_t sync_snapshot(const service::MatterBridgeSnapshot& snapshot) noexcept;
    std::size_t drain_attribute_updates(MatterAttributeUpdate* out, std::size_t capacity) noexcept;

private:
    struct DeviceShadow {
        bool in_use{false};
        uint16_t short_addr{0};
        uint16_t status_endpoint{0};
        bool online{false};
        bool stale{false};
        bool has_temperature{false};
        int16_t temperature_centi_c{0};
        bool has_occupancy{false};
        bool occupied{false};
        bool has_contact{false};
        bool contact_open{false};
    };

    void reset_sync_state() noexcept;
#ifdef ESP_PLATFORM
    static void task_entry(void* arg) noexcept;
    void run_loop() noexcept;
    bool ensure_task_started() noexcept;
#endif

    std::atomic<bool> started_{false};
    MatterEndpointMapEntry endpoint_map_[kMatterMaxEndpointMapEntries]{};
    std::size_t endpoint_map_size_{0};
    service::MatterBridgeSnapshot runtime_snapshot_cache_{};
    DeviceShadow cached_devices_[service::kServiceMaxDevices]{};
    DeviceShadow sync_shadow_scratch_[service::kServiceMaxDevices]{};
    std::size_t pending_update_count_{0};
    MatterAttributeUpdate pending_updates_[kMatterMaxUpdatesPerSync]{};
    service::MatterRuntimeApi* runtime_{nullptr};
#ifdef ESP_PLATFORM
    void* task_handle_{nullptr};
#endif
};

}  // namespace matter_bridge
