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

    // A single WireRecord-sized scratch buffer, reused sequentially for
    // both slots -- NOT two simultaneous ~2.2KB locals as this function
    // originally had. That doubled footprint (~4.4KB) alone consumed
    // roughly half of the service_runtime task's entire 9216-byte stack
    // budget and caused a real "Guru Meditation Error: Stack protection
    // fault" observed on real ESP32-C6 hardware during this device's
    // first-ever real boot -- restore_persisted_core_state() runs on
    // every single boot, so this was not a rare edge case but a
    // guaranteed crash. Only slot A's small header (20 bytes) needs to
    // outlive slot B's read; its payload is staged directly into the
    // caller-owned `*out` instead of being kept in a second local copy.
    WireRecord record{};
    uint32_t len = 0U;

    const hal_nvs_status_t status_a = hal_nvs_get_blob(kPersistedStateKeySlotA, &record, sizeof(record), &len);
    const bool present_a = status_a != HAL_NVS_STATUS_NOT_FOUND;
    const bool valid_a = status_a == HAL_NVS_STATUS_OK && validate(record, len);
    PersistedStateHeader header_a{};
    if (valid_a) {
        header_a = record.header;
        *out = record.payload;
    }

    const hal_nvs_status_t status_b = hal_nvs_get_blob(kPersistedStateKeySlotB, &record, sizeof(record), &len);
    const bool present_b = status_b != HAL_NVS_STATUS_NOT_FOUND;
    const bool valid_b = status_b == HAL_NVS_STATUS_OK && validate(record, len);

    if (!present_a && !present_b) {
        return LoadResult::kNotFound;
    }
    if (!valid_a && !valid_b) {
        return LoadResult::kCorrupt;
    }

    // Equivalent (De Morgan) to the original single-expression tie break
    // "choose A if valid_a && (!valid_b || gen_a >= gen_b), else choose
    // B" -- ties (equal generation) still prefer A, unchanged.
    if (!valid_a || (valid_b && header_a.generation < record.header.generation)) {
        *out = record.payload;  // B wins -- overwrite the staged A payload
    }
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

    // A single WireRecord-sized scratch buffer, reused for both the write
    // and the readback verification below -- NOT two simultaneous
    // ~2.2KB locals (`record` + `readback`) as this function originally
    // had, the same real stack-overflow pattern found and fixed in
    // load() above (see that function's own comment for the real crash
    // this caused on real ESP32-C6 hardware). Only the small
    // (4-byte) written generation number needs to survive the reuse.
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
    const uint32_t written_generation = record.header.generation;

    uint32_t readback_len = 0U;
    if (hal_nvs_get_blob(target_key, &record, sizeof(record), &readback_len) != HAL_NVS_STATUS_OK) {
        return false;
    }
    if (!validate(record, readback_len) || record.header.generation != written_generation) {
        return false;
    }

    return true;
}

}  // namespace service
