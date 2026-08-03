/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "device_id.hpp"
#include "service_public_types.hpp"

namespace service {

// Service-owned mutable locator registry (FD-02). Maintains the bidirectional
// DeviceId <-> short_addr mapping described in plan Section 7.2. This class
// owns only the mapping/index; it never mutates Core and never touches
// persistence, HAL or the domain event bus directly — those are the caller's
// responsibility (ServiceRuntime), matching the 8-step remap algorithm where
// steps 1-5 are this registry's job and steps 6-8 (domain event, persist,
// resync) belong to the caller.
enum class DeviceLocatorStatus : uint8_t {
    kOffline = 0,
    kOnline = 1,
};

struct DeviceLocatorEntry {
    core::DeviceId device_id{};
    uint16_t short_addr{kUnknownShortAddr};
    DeviceLocatorStatus status{DeviceLocatorStatus::kOffline};
    uint32_t mapping_revision{0};
};

enum class DeviceLocatorRemapResult : uint8_t {
    // A brand-new DeviceId<->short_addr mapping was created.
    kInserted = 0,
    // The same DeviceId moved to a new short_addr (its old reverse mapping,
    // if any, was released first).
    kUpdated = 1,
    // The requested short_addr previously belonged to a different DeviceId;
    // that other device's locator entry is marked offline (INV-ID-03: it does
    // not transfer config/state to the new owner) and the new mapping is
    // installed.
    kDisplaced = 2,
    kInvalidArgument = 3,
    kNoCapacity = 4,
};

class DeviceLocatorRegistry {
public:
    static constexpr std::size_t kMaxEntries = kServiceMaxDevices;

    // Installs/updates the bidirectional mapping for `device_id` -> `short_addr`.
    // Always assigns a new, strictly increasing mapping_revision and writes it
    // to `*revision_out` (when non-null) regardless of which result variant is
    // returned, except kInvalidArgument/kNoCapacity which never mutate state.
    DeviceLocatorRemapResult remap(
        const core::DeviceId& device_id,
        uint16_t short_addr,
        uint32_t* revision_out) noexcept;

    // Marks the entry for `device_id` offline without removing or transferring
    // its mapping (INV-ID-03: a later remap of the same short_addr to a
    // different DeviceId must not resurrect this entry's data).
    bool mark_offline(const core::DeviceId& device_id) noexcept;

    // Fully removes the entry for `device_id`, freeing its slot.
    bool remove(const core::DeviceId& device_id) noexcept;

    bool find_by_device_id(const core::DeviceId& device_id, DeviceLocatorEntry* out) const noexcept;
    bool find_by_short_addr(uint16_t short_addr, DeviceLocatorEntry* out) const noexcept;

    std::size_t size() const noexcept { return count_; }
    void clear() noexcept;

private:
    int find_slot_by_device_id(const core::DeviceId& device_id) const noexcept;
    int find_slot_by_short_addr(uint16_t short_addr) const noexcept;
    int find_free_slot() const noexcept;

    std::array<DeviceLocatorEntry, kMaxEntries> entries_{};
    std::array<bool, kMaxEntries> in_use_{};
    std::size_t count_{0};
    uint32_t next_revision_{1};
};

}  // namespace service
