/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "persisted_state_store.hpp"

#include <cstring>
#include <type_traits>

#include "crc32c.hpp"
#include "hal_nvs.h"

namespace service {

namespace {

static_assert(std::is_trivially_copyable<PersistedStatePayload>::value, "PersistedStatePayload must be POD-like");

struct WireRecord {
    PersistedStateHeader header{};
    PersistedStatePayload payload{};
};

bool validate(const WireRecord& record, uint32_t read_len) noexcept {
    if (read_len != sizeof(WireRecord)) {
        return false;
    }
    if (record.header.magic != kPersistedStateMagic || record.header.schema_version != kPersistedStateSchemaVersion) {
        return false;
    }
    if (record.header.payload_length != sizeof(PersistedStatePayload)) {
        return false;
    }
    const uint32_t computed_crc = common::crc32c(&record.payload, sizeof(record.payload));
    return computed_crc == record.header.payload_crc32c;
}

}  // namespace

PersistedStateStore::SlotProbe PersistedStateStore::probe_slot(const char* key) const noexcept {
    SlotProbe probe{};
    WireRecord record{};
    uint32_t read_len = 0U;
    const hal_nvs_status_t status = hal_nvs_get_blob(key, &record, sizeof(record), &read_len);
    if (status != HAL_NVS_STATUS_OK) {
        return probe;
    }
    if (!validate(record, read_len)) {
        return probe;
    }
    probe.valid = true;
    probe.generation = record.header.generation;
    return probe;
}

PersistedStateStore::LoadResult PersistedStateStore::load(PersistedStatePayload* out) const noexcept {
    if (out == nullptr) {
        return LoadResult::kCorrupt;
    }

    WireRecord record_a{};
    WireRecord record_b{};
    uint32_t len_a = 0U;
    uint32_t len_b = 0U;
    const hal_nvs_status_t status_a = hal_nvs_get_blob(kPersistedStateKeySlotA, &record_a, sizeof(record_a), &len_a);
    const hal_nvs_status_t status_b = hal_nvs_get_blob(kPersistedStateKeySlotB, &record_b, sizeof(record_b), &len_b);

    const bool present_a = status_a != HAL_NVS_STATUS_NOT_FOUND;
    const bool present_b = status_b != HAL_NVS_STATUS_NOT_FOUND;
    if (!present_a && !present_b) {
        return LoadResult::kNotFound;
    }

    const bool valid_a = status_a == HAL_NVS_STATUS_OK && validate(record_a, len_a);
    const bool valid_b = status_b == HAL_NVS_STATUS_OK && validate(record_b, len_b);

    if (!valid_a && !valid_b) {
        return LoadResult::kCorrupt;
    }

    const WireRecord& chosen =
        (valid_a && (!valid_b || record_a.header.generation >= record_b.header.generation)) ? record_a : record_b;
    *out = chosen.payload;
    return LoadResult::kLoaded;
}

bool PersistedStateStore::save(const PersistedStatePayload& payload) noexcept {
    const SlotProbe probe_a = probe_slot(kPersistedStateKeySlotA);
    const SlotProbe probe_b = probe_slot(kPersistedStateKeySlotB);

    // Target whichever slot is NOT the current highest-generation valid
    // slot, so the other one remains a safe rollback copy until this write
    // is confirmed by readback below.
    const bool a_is_current_best =
        probe_a.valid && (!probe_b.valid || probe_a.generation >= probe_b.generation);
    const char* target_key = a_is_current_best ? kPersistedStateKeySlotB : kPersistedStateKeySlotA;

    const uint32_t current_generation = a_is_current_best ? probe_a.generation : (probe_b.valid ? probe_b.generation : 0U);
    const uint32_t other_slot_generation = a_is_current_best ? (probe_b.valid ? probe_b.generation : 0U) : probe_a.generation;
    const uint32_t base_generation = current_generation > other_slot_generation ? current_generation : other_slot_generation;

    WireRecord record{};
    record.header.magic = kPersistedStateMagic;
    record.header.schema_version = kPersistedStateSchemaVersion;
    record.header.generation = base_generation + 1U;
    record.header.payload_length = sizeof(PersistedStatePayload);
    record.payload = payload;
    record.header.payload_crc32c = common::crc32c(&record.payload, sizeof(record.payload));

    if (hal_nvs_set_blob(target_key, &record, sizeof(record)) != HAL_NVS_STATUS_OK) {
        return false;
    }

    WireRecord readback{};
    uint32_t readback_len = 0U;
    if (hal_nvs_get_blob(target_key, &readback, sizeof(readback), &readback_len) != HAL_NVS_STATUS_OK) {
        return false;
    }
    if (!validate(readback, readback_len) || readback.header.generation != record.header.generation) {
        return false;
    }

    return true;
}

}  // namespace service
