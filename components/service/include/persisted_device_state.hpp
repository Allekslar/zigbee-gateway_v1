/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core_state.hpp"
#include "device_id.hpp"

namespace service {

// Explicit, versioned persisted device-state schema (plan Section 7.3 /
// INV-PERS-01): every field is named and converted explicitly to/from the
// live core::CoreState via to_payload()/apply_payload() below. Nothing here
// reinterpret_casts core::CoreState directly, so a future change to that
// live struct's layout cannot silently corrupt or misread persisted data --
// the conversion functions must be updated explicitly instead.
inline constexpr uint32_t kPersistedStateMagic = 0x5A475732U;  // "ZGW2"
inline constexpr uint32_t kPersistedStateSchemaVersion = 2U;
inline constexpr const char* kPersistedStateKeySlotA = "dstate_a";
inline constexpr const char* kPersistedStateKeySlotB = "dstate_b";

enum class PersistedMatterEndpointState : uint8_t {
    kUnassigned = 0,
    kAssigned = 1,
    kPendingRemoval = 2,
};

// Mirrors core::CoreDeviceRecord's observable fields explicitly (FD-01: the
// durable key is device_id; last_short_addr is locator metadata only and is
// never trusted as identity on restore). `valid` marks a populated slot,
// matching the plan's "record count and per-record validity" requirement.
struct PersistedDeviceRecord {
    bool valid{false};
    std::array<uint8_t, core::DeviceId::kByteLength> device_id_bytes{};
    uint16_t last_short_addr{core::kUnknownDeviceShortAddr};
    bool power_on{false};
    uint8_t reporting_state{0};
    bool has_temperature{false};
    int16_t temperature_centi_c{0};
    uint8_t occupancy_state{0};
    uint8_t contact_state{0};
    bool contact_tamper{false};
    bool contact_battery_low{false};
    bool has_battery{false};
    uint8_t battery_percent{0};
    bool has_battery_voltage{false};
    uint16_t battery_voltage_mv{0};
    bool has_lqi{false};
    uint8_t lqi{0};
    bool has_rssi{false};
    int8_t rssi_dbm{0};
    // Optional Matter endpoint assignment (S4 is the sole allocator; S3 only
    // needs to represent and round-trip unassigned/assigned/pending-removal
    // so a later stage can populate it without another schema bump).
    PersistedMatterEndpointState matter_endpoint_state{PersistedMatterEndpointState::kUnassigned};
    uint8_t matter_endpoint_id{0};
};

struct PersistedStatePayload {
    uint32_t device_count{0};
    std::array<PersistedDeviceRecord, core::kMaxDevices> devices{};
};

// Two-generation write protocol header (plan Section 7.3): magic, schema
// version, generation, payload length and a CRC32C integrity field distinct
// from the NVS layer's own transport integrity. `generation` is the sole
// commit marker: on load, the highest-generation slot that also passes
// magic/schema/length/CRC validation is authoritative, so an interrupted
// write to the inactive slot can never be mistaken for the active one.
struct PersistedStateHeader {
    uint32_t magic{kPersistedStateMagic};
    uint32_t schema_version{kPersistedStateSchemaVersion};
    uint32_t generation{0};
    uint32_t payload_length{0};
    uint32_t payload_crc32c{0};
};

struct PersistedStateRecord {
    PersistedStateHeader header{};
    PersistedStatePayload payload{};
};

// Explicit conversion, never a raw reinterpret_cast of core::CoreState.
// to_payload() drops any device slot whose device_id is not core::DeviceId
// -valid (FD-01: never persist/carry forward an identity-less record under
// the new schema -- see docs/architecture/DEVICE_IDENTITY.md).
PersistedStatePayload to_payload(const core::CoreState& state) noexcept;

// Rebuilds a sanitized core::CoreState from a validated payload: every
// restored device is forced offline, pending command/session state is never
// resurrected (plan Section 9 S3 "sanitize restored runtime fields").
core::CoreState apply_payload(const PersistedStatePayload& payload) noexcept;

}  // namespace service
