/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "matter_endpoint_registry.hpp"

#include <algorithm>
#include <type_traits>

#include "crc32c.hpp"
#include "hal_nvs.h"

namespace service {

namespace {

// Own two-generation write/validate/commit protocol (plan Section 7.3),
// deliberately not reusing PersistedStateStore/PersistedStatePayload: this
// registry's data must survive a device's CoreDeviceRecord being wiped
// entirely (see the class comment in matter_endpoint_registry.hpp), so it
// cannot ride on the per-device S3 payload. The protocol itself mirrors
// persisted_state_store.cpp exactly, just over a differently-shaped, much
// smaller payload.
constexpr uint32_t kMatterEndpointStoreMagic = 0x5A47574DU;  // "ZGWM"
constexpr uint32_t kMatterEndpointStoreSchemaVersion = 1U;
constexpr const char* kMatterEndpointStoreKeySlotA = "mtep_a";
constexpr const char* kMatterEndpointStoreKeySlotB = "mtep_b";

struct WireEntry {
    bool valid{false};
    std::array<uint8_t, core::DeviceId::kByteLength> device_id_bytes{};
    uint8_t state{0};
};

struct WirePayload {
    std::array<WireEntry, MatterEndpointRegistry::kCapacity> entries{};
};

static_assert(std::is_trivially_copyable<WirePayload>::value, "WirePayload must be POD-like");

struct WireHeader {
    uint32_t magic{kMatterEndpointStoreMagic};
    uint32_t schema_version{kMatterEndpointStoreSchemaVersion};
    uint32_t generation{0};
    uint32_t payload_length{0};
    uint32_t payload_crc32c{0};
};

struct WireRecord {
    WireHeader header{};
    WirePayload payload{};
};

bool validate(const WireRecord& record, uint32_t read_len) noexcept {
    if (read_len != sizeof(WireRecord)) {
        return false;
    }
    if (record.header.magic != kMatterEndpointStoreMagic ||
        record.header.schema_version != kMatterEndpointStoreSchemaVersion) {
        return false;
    }
    if (record.header.payload_length != sizeof(WirePayload)) {
        return false;
    }
    return common::crc32c(&record.payload, sizeof(record.payload)) == record.header.payload_crc32c;
}

struct SlotProbe {
    bool valid{false};
    uint32_t generation{0};
};

SlotProbe probe_slot(const char* key) noexcept {
    SlotProbe probe{};
    WireRecord record{};
    uint32_t read_len = 0U;
    if (hal_nvs_get_blob(key, &record, sizeof(record), &read_len) != HAL_NVS_STATUS_OK) {
        return probe;
    }
    if (!validate(record, read_len)) {
        return probe;
    }
    probe.valid = true;
    probe.generation = record.header.generation;
    return probe;
}

}  // namespace

void MatterEndpointRegistry::load() noexcept {
    entries_ = std::array<Entry, kCapacity>{};

    WireRecord record_a{};
    WireRecord record_b{};
    uint32_t len_a = 0U;
    uint32_t len_b = 0U;
    const hal_nvs_status_t status_a =
        hal_nvs_get_blob(kMatterEndpointStoreKeySlotA, &record_a, sizeof(record_a), &len_a);
    const hal_nvs_status_t status_b =
        hal_nvs_get_blob(kMatterEndpointStoreKeySlotB, &record_b, sizeof(record_b), &len_b);

    const bool valid_a = status_a == HAL_NVS_STATUS_OK && validate(record_a, len_a);
    const bool valid_b = status_b == HAL_NVS_STATUS_OK && validate(record_b, len_b);
    if (!valid_a && !valid_b) {
        return;  // Not found or corrupt: start empty (fail-safe; see header comment).
    }

    const WireRecord& chosen =
        (valid_a && (!valid_b || record_a.header.generation >= record_b.header.generation)) ? record_a : record_b;

    for (std::size_t i = 0; i < kCapacity; ++i) {
        const WireEntry& wire = chosen.payload.entries[i];
        if (!wire.valid) {
            continue;
        }
        Entry& entry = entries_[i];
        entry.valid = true;
        entry.device_id = core::DeviceId(wire.device_id_bytes);
        entry.state = static_cast<MatterEndpointState>(wire.state);
    }
}

bool MatterEndpointRegistry::save() noexcept {
    const SlotProbe probe_a = probe_slot(kMatterEndpointStoreKeySlotA);
    const SlotProbe probe_b = probe_slot(kMatterEndpointStoreKeySlotB);

    const bool a_is_current_best = probe_a.valid && (!probe_b.valid || probe_a.generation >= probe_b.generation);
    const char* target_key = a_is_current_best ? kMatterEndpointStoreKeySlotB : kMatterEndpointStoreKeySlotA;

    const uint32_t current_generation = a_is_current_best ? probe_a.generation : (probe_b.valid ? probe_b.generation : 0U);
    const uint32_t other_slot_generation =
        a_is_current_best ? (probe_b.valid ? probe_b.generation : 0U) : probe_a.generation;
    const uint32_t base_generation = current_generation > other_slot_generation ? current_generation : other_slot_generation;

    WireRecord record{};
    record.header.magic = kMatterEndpointStoreMagic;
    record.header.schema_version = kMatterEndpointStoreSchemaVersion;
    record.header.generation = base_generation + 1U;
    record.header.payload_length = sizeof(WirePayload);
    for (std::size_t i = 0; i < kCapacity; ++i) {
        const Entry& entry = entries_[i];
        WireEntry& wire = record.payload.entries[i];
        wire.valid = entry.valid;
        wire.device_id_bytes = entry.device_id.bytes();
        wire.state = static_cast<uint8_t>(entry.state);
    }
    record.header.payload_crc32c = common::crc32c(&record.payload, sizeof(record.payload));

    if (hal_nvs_set_blob(target_key, &record, sizeof(record)) != HAL_NVS_STATUS_OK) {
        return false;
    }

    WireRecord readback{};
    uint32_t readback_len = 0U;
    if (hal_nvs_get_blob(target_key, &readback, sizeof(readback), &readback_len) != HAL_NVS_STATUS_OK) {
        return false;
    }
    return validate(readback, readback_len) && readback.header.generation == record.header.generation;
}

int MatterEndpointRegistry::find_index_by_device_id(const core::DeviceId& device_id) const noexcept {
    for (std::size_t i = 0; i < kCapacity; ++i) {
        if (entries_[i].valid && entries_[i].device_id == device_id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

MatterEndpointAllocateResult MatterEndpointRegistry::allocate(
    const core::DeviceId& device_id, uint8_t* endpoint_out) noexcept {
    if (!device_id.valid() || endpoint_out == nullptr) {
        return MatterEndpointAllocateResult::kInvalidArgument;
    }

    const int existing_index = find_index_by_device_id(device_id);
    if (existing_index >= 0 &&
        entries_[static_cast<std::size_t>(existing_index)].state == MatterEndpointState::kAssigned) {
        *endpoint_out = kEndpointBase + static_cast<uint8_t>(existing_index);
        return MatterEndpointAllocateResult::kAlreadyAssigned;
    }

    // Lowest free slot: only a fully-unassigned slot is eligible. A
    // kPendingRemoval slot for a *different* device_id is intentionally
    // excluded -- item 24: no reuse before tombstone confirmation. (The
    // existing_index check above already handled the "same device_id,
    // still kAssigned" idempotent case; a same-device_id kPendingRemoval
    // entry is deliberately NOT treated as reusable either -- a rejoin
    // mid-removal gets a fresh slot, matching FD-01's no-state-inheritance
    // rule extended to Matter identity.)
    for (std::size_t i = 0; i < kCapacity; ++i) {
        if (entries_[i].valid) {
            continue;
        }
        entries_[i].valid = true;
        entries_[i].device_id = device_id;
        entries_[i].state = MatterEndpointState::kAssigned;
        if (!save()) {
            entries_[i] = Entry{};
            return MatterEndpointAllocateResult::kPersistFailed;
        }
        *endpoint_out = kEndpointBase + static_cast<uint8_t>(i);
        return MatterEndpointAllocateResult::kAssigned;
    }

    return MatterEndpointAllocateResult::kNoCapacity;
}

bool MatterEndpointRegistry::find(const core::DeviceId& device_id, uint8_t* endpoint_out) const noexcept {
    if (!device_id.valid() || endpoint_out == nullptr) {
        return false;
    }
    const int index = find_index_by_device_id(device_id);
    if (index < 0 || entries_[static_cast<std::size_t>(index)].state != MatterEndpointState::kAssigned) {
        return false;
    }
    *endpoint_out = kEndpointBase + static_cast<uint8_t>(index);
    return true;
}

bool MatterEndpointRegistry::mark_pending_removal(const core::DeviceId& device_id, uint8_t* endpoint_out) noexcept {
    if (!device_id.valid() || endpoint_out == nullptr) {
        return false;
    }
    const int index = find_index_by_device_id(device_id);
    if (index < 0) {
        return false;
    }
    Entry& entry = entries_[static_cast<std::size_t>(index)];
    if (entry.state != MatterEndpointState::kAssigned) {
        return false;
    }
    entry.state = MatterEndpointState::kPendingRemoval;
    if (!save()) {
        entry.state = MatterEndpointState::kAssigned;
        return false;
    }
    *endpoint_out = kEndpointBase + static_cast<uint8_t>(index);
    return true;
}

bool MatterEndpointRegistry::confirm_removed(const core::DeviceId& device_id) noexcept {
    const int index = find_index_by_device_id(device_id);
    if (index < 0) {
        return false;
    }
    Entry& entry = entries_[static_cast<std::size_t>(index)];
    if (entry.state != MatterEndpointState::kPendingRemoval) {
        return false;
    }
    const Entry backup = entry;
    entry = Entry{};
    if (!save()) {
        entry = backup;
        return false;
    }
    return true;
}

std::size_t MatterEndpointRegistry::occupied_count() const noexcept {
    return static_cast<std::size_t>(
        std::count_if(entries_.begin(), entries_.end(), [](const Entry& entry) noexcept { return entry.valid; }));
}

}  // namespace service
