/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "device_locator_registry.hpp"

namespace service {

int DeviceLocatorRegistry::find_slot_by_device_id(const core::DeviceId& device_id) const noexcept {
    for (std::size_t i = 0; i < kMaxEntries; ++i) {
        if (in_use_[i] && entries_[i].device_id == device_id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int DeviceLocatorRegistry::find_slot_by_short_addr(uint16_t short_addr) const noexcept {
    // Only an online entry may be the current authoritative owner of a
    // short_addr (INV-ID-02); an offline entry keeps its last-known
    // short_addr purely for diagnostics and must not match here.
    for (std::size_t i = 0; i < kMaxEntries; ++i) {
        if (in_use_[i] && entries_[i].status == DeviceLocatorStatus::kOnline &&
            entries_[i].short_addr == short_addr) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int DeviceLocatorRegistry::find_free_slot() const noexcept {
    for (std::size_t i = 0; i < kMaxEntries; ++i) {
        if (!in_use_[i]) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

DeviceLocatorRemapResult DeviceLocatorRegistry::remap(
    const core::DeviceId& device_id,
    uint16_t short_addr,
    uint32_t* revision_out) noexcept {
    if (!device_id.valid() || short_addr == kUnknownShortAddr) {
        return DeviceLocatorRemapResult::kInvalidArgument;
    }

    const int existing_slot = find_slot_by_device_id(device_id);
    int target_slot = existing_slot;
    if (target_slot < 0) {
        target_slot = find_free_slot();
        if (target_slot < 0) {
            return DeviceLocatorRemapResult::kNoCapacity;
        }
    }

    // Steps 2-3 of the plan's remap algorithm: the old reverse mapping for
    // this DeviceId is implicitly released below (target_slot's short_addr
    // field is simply overwritten); detect whether the requested short_addr
    // currently belongs to a different, still-online DeviceId.
    const int short_addr_owner_slot = find_slot_by_short_addr(short_addr);

    DeviceLocatorRemapResult result =
        (existing_slot >= 0) ? DeviceLocatorRemapResult::kUpdated : DeviceLocatorRemapResult::kInserted;

    if (short_addr_owner_slot >= 0 && short_addr_owner_slot != target_slot) {
        // Step 4: mark the displaced device's locator offline without
        // transferring its config/state (INV-ID-03). Its short_addr field is
        // intentionally left as a diagnostic breadcrumb; find_slot_by_short_addr
        // ignores offline entries, so it can never be matched as an owner again.
        entries_[static_cast<std::size_t>(short_addr_owner_slot)].status = DeviceLocatorStatus::kOffline;
        result = DeviceLocatorRemapResult::kDisplaced;
    }

    if (!in_use_[static_cast<std::size_t>(target_slot)]) {
        in_use_[static_cast<std::size_t>(target_slot)] = true;
        ++count_;
    }

    // Step 5: install the new bidirectional mapping.
    DeviceLocatorEntry& entry = entries_[static_cast<std::size_t>(target_slot)];
    entry.device_id = device_id;
    entry.short_addr = short_addr;
    entry.status = DeviceLocatorStatus::kOnline;
    entry.mapping_revision = next_revision_++;

    if (revision_out != nullptr) {
        *revision_out = entry.mapping_revision;
    }
    return result;
}

bool DeviceLocatorRegistry::mark_offline(const core::DeviceId& device_id) noexcept {
    const int slot = find_slot_by_device_id(device_id);
    if (slot < 0) {
        return false;
    }
    entries_[static_cast<std::size_t>(slot)].status = DeviceLocatorStatus::kOffline;
    return true;
}

bool DeviceLocatorRegistry::remove(const core::DeviceId& device_id) noexcept {
    const int slot = find_slot_by_device_id(device_id);
    if (slot < 0) {
        return false;
    }
    entries_[static_cast<std::size_t>(slot)] = DeviceLocatorEntry{};
    in_use_[static_cast<std::size_t>(slot)] = false;
    --count_;
    return true;
}

bool DeviceLocatorRegistry::find_by_device_id(const core::DeviceId& device_id, DeviceLocatorEntry* out) const noexcept {
    const int slot = find_slot_by_device_id(device_id);
    if (slot < 0) {
        return false;
    }
    if (out != nullptr) {
        *out = entries_[static_cast<std::size_t>(slot)];
    }
    return true;
}

bool DeviceLocatorRegistry::find_by_short_addr(uint16_t short_addr, DeviceLocatorEntry* out) const noexcept {
    const int slot = find_slot_by_short_addr(short_addr);
    if (slot < 0) {
        return false;
    }
    if (out != nullptr) {
        *out = entries_[static_cast<std::size_t>(slot)];
    }
    return true;
}

void DeviceLocatorRegistry::clear() noexcept {
    entries_ = {};
    in_use_ = {};
    count_ = 0;
    next_revision_ = 1;
}

}  // namespace service
