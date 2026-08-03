/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "device_locator_registry.hpp"

namespace {

core::DeviceId make_id(const char* hex) {
    core::DeviceId id{};
    const bool ok = core::DeviceId::parse(hex, 16, &id);
    assert(ok);
    return id;
}

}  // namespace

int main() {
    using service::DeviceLocatorEntry;
    using service::DeviceLocatorRegistry;
    using service::DeviceLocatorRemapResult;
    using service::DeviceLocatorStatus;

    const core::DeviceId device_a = make_id("00124b0001aaaaaa");
    const core::DeviceId device_b = make_id("00124b0001bbbbbb");

    // --- Basic insert ---
    DeviceLocatorRegistry registry;
    uint32_t rev1 = 0;
    assert(registry.remap(device_a, 0x1111U, &rev1) == DeviceLocatorRemapResult::kInserted);
    assert(rev1 != 0U);
    assert(registry.size() == 1U);

    DeviceLocatorEntry entry{};
    assert(registry.find_by_device_id(device_a, &entry));
    assert(entry.short_addr == 0x1111U);
    assert(entry.status == DeviceLocatorStatus::kOnline);
    assert(entry.mapping_revision == rev1);

    assert(registry.find_by_short_addr(0x1111U, &entry));
    assert(entry.device_id == device_a);

    // --- INV-ID-01: same EUI-64, new short_addr updates the SAME record ---
    uint32_t rev2 = 0;
    assert(registry.remap(device_a, 0x2222U, &rev2) == DeviceLocatorRemapResult::kUpdated);
    assert(rev2 > rev1);
    assert(registry.size() == 1U);  // still one record, not two

    assert(!registry.find_by_short_addr(0x1111U, &entry));  // old locator released
    assert(registry.find_by_short_addr(0x2222U, &entry));
    assert(entry.device_id == device_a);

    // --- INV-ID-02 / INV-ID-03: a different EUI-64 reusing an old short_addr
    // creates a distinct record and does NOT inherit anything from device_a ---
    uint32_t rev3 = 0;
    assert(registry.remap(device_b, 0x1111U, &rev3) == DeviceLocatorRemapResult::kInserted);
    assert(registry.size() == 2U);
    assert(registry.find_by_short_addr(0x1111U, &entry));
    assert(entry.device_id == device_b);
    assert(entry.mapping_revision == rev3);  // device_b's own fresh revision, unrelated to device_a's

    // --- Displacement: device_b now claims device_a's CURRENT short_addr (0x2222) ---
    uint32_t rev4 = 0;
    assert(registry.remap(device_b, 0x2222U, &rev4) == DeviceLocatorRemapResult::kDisplaced);
    assert(registry.size() == 2U);  // no new record created, device_a's entry persists but offline

    // device_a's locator no longer resolves via short_addr (displaced/offline).
    assert(registry.find_by_short_addr(0x2222U, &entry));
    assert(entry.device_id == device_b);  // device_b is now the sole owner of 0x2222

    DeviceLocatorEntry displaced{};
    assert(registry.find_by_device_id(device_a, &displaced));
    assert(displaced.status == DeviceLocatorStatus::kOffline);

    // device_a's old short_addr (0x1111, which device_b vacated by moving to
    // 0x2222) is now unclaimed by anyone online.
    assert(!registry.find_by_short_addr(0x1111U, &entry));

    // --- mark_offline / remove ---
    DeviceLocatorRegistry solo;
    uint32_t rev5 = 0;
    assert(solo.remap(device_a, 0x3333U, &rev5) == DeviceLocatorRemapResult::kInserted);
    assert(solo.mark_offline(device_a));
    assert(!solo.find_by_short_addr(0x3333U, &entry));  // offline no longer resolves by short_addr
    assert(solo.find_by_device_id(device_a, &entry));   // but the record itself is still present
    assert(entry.status == DeviceLocatorStatus::kOffline);
    assert(!solo.mark_offline(device_b));  // unknown device_id

    assert(solo.remove(device_a));
    assert(solo.size() == 0U);
    assert(!solo.find_by_device_id(device_a, &entry));
    assert(!solo.remove(device_a));  // already removed

    // --- Invalid arguments never mutate state ---
    DeviceLocatorRegistry guard;
    core::DeviceId invalid_id{};  // default all-zero, invalid
    assert(!invalid_id.valid());
    uint32_t rev_invalid = 0;
    assert(guard.remap(invalid_id, 0x4444U, &rev_invalid) == DeviceLocatorRemapResult::kInvalidArgument);
    assert(guard.remap(device_a, service::kUnknownShortAddr, &rev_invalid) ==
           DeviceLocatorRemapResult::kInvalidArgument);
    assert(guard.size() == 0U);

    // --- Capacity boundary ---
    DeviceLocatorRegistry full;
    for (std::size_t i = 0; i < DeviceLocatorRegistry::kMaxEntries; ++i) {
        std::array<uint8_t, core::DeviceId::kByteLength> bytes{};
        bytes[6] = static_cast<uint8_t>((i >> 8U) & 0xFFU);
        bytes[7] = static_cast<uint8_t>(i & 0xFFU);
        bytes[0] = 0xAAU;  // keep non-zero/non-ff so it stays a valid id
        const core::DeviceId id(bytes);
        uint32_t rev = 0;
        assert(full.remap(id, static_cast<uint16_t>(0x1000U + i), &rev) == DeviceLocatorRemapResult::kInserted);
    }
    assert(full.size() == DeviceLocatorRegistry::kMaxEntries);

    std::array<uint8_t, core::DeviceId::kByteLength> overflow_bytes{};
    overflow_bytes[0] = 0xBBU;
    const core::DeviceId overflow_id(overflow_bytes);
    uint32_t rev_overflow = 0;
    assert(full.remap(overflow_id, 0x9999U, &rev_overflow) == DeviceLocatorRemapResult::kNoCapacity);
    assert(full.size() == DeviceLocatorRegistry::kMaxEntries);  // unchanged on failure

    // --- clear() ---
    full.clear();
    assert(full.size() == 0U);

    return 0;
}
