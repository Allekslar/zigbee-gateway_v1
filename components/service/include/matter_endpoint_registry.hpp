/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "device_id.hpp"
#include "service_public_types.hpp"

namespace service {

enum class MatterEndpointState : uint8_t {
    kUnassigned = 0,
    kAssigned = 1,
    kPendingRemoval = 2,
};

enum class MatterEndpointAllocateResult : uint8_t {
    kAssigned = 0,
    kAlreadyAssigned = 1,
    kNoCapacity = 2,
    kInvalidArgument = 3,
    kPersistFailed = 4,
};

// DeviceId-keyed Matter endpoint allocator (plan FD-16 / S4 required
// changes #20-26). One endpoint carries every Matter cluster a single
// physical device supports; short_addr never participates in this
// mapping, so a locator remap (rejoin at a new short_addr) never disturbs
// an existing allocation (INV-ID-*). Endpoints are drawn from a fixed,
// reviewed dynamic range [kEndpointBase, kEndpointRangeEnd] sized to
// service::kServiceMaxDevices (capacity 64 -> range 10-73); allocation is
// deterministic lowest-free-slot. Removal is two-phase: kPendingRemoval
// blocks reuse until the Matter adapter confirms the tombstone, so an
// endpoint can never be silently reassigned to a different physical
// device before the previous owner's identity is fully retired.
//
// This class owns its own small durable store, independent of S3's
// PersistedStateStore/PersistedDeviceRecord. A device leaving Core wipes
// its CoreDeviceRecord immediately (core_reducer.cpp kDeviceLeft), but a
// pending-removal Matter endpoint must remain non-reusable across that
// wipe and across reboot until tombstone confirmation -- once the device
// has left, there is no CoreState device slot left to carry that fact
// forward, so it cannot be represented as a per-device field riding on the
// S3 payload.
class MatterEndpointRegistry {
public:
    static constexpr uint8_t kEndpointBase = 10U;
    static constexpr std::size_t kCapacity = kServiceMaxDevices;
    static constexpr uint8_t kEndpointRangeEnd = kEndpointBase + static_cast<uint8_t>(kCapacity) - 1U;

    // Loads persisted state from NVS. A missing or corrupt store starts
    // empty (fail-safe, not fail-closed): every previously assigned
    // endpoint becomes re-allocatable on next join, which is recoverable
    // (a fresh Matter identity is assigned), unlike losing core::DeviceId
    // itself.
    void load() noexcept;

    // Returns the existing endpoint for device_id (kAlreadyAssigned,
    // idempotent) or assigns and persists the lowest free slot in range
    // (kAssigned). kNoCapacity if every slot is occupied (kAssigned or
    // kPendingRemoval); kPersistFailed rolls the in-memory assignment back
    // rather than expose an endpoint that is not durable.
    MatterEndpointAllocateResult allocate(const core::DeviceId& device_id, uint8_t* endpoint_out) noexcept;

    // Read-only lookup; never allocates. Only an kAssigned entry resolves;
    // a kPendingRemoval entry is intentionally not resolvable here (it is
    // being retired, not published).
    bool find(const core::DeviceId& device_id, uint8_t* endpoint_out) const noexcept;

    // Step 1 of removal: transitions an kAssigned entry to kPendingRemoval
    // (still occupies its endpoint; no longer resolvable via find()) and
    // persists the transition before returning. Returns the endpoint being
    // retired so the caller can attempt the Matter-adapter tombstone.
    bool mark_pending_removal(const core::DeviceId& device_id, uint8_t* endpoint_out) noexcept;

    // Step 2: called only after the Matter adapter confirms the tombstone.
    // Frees the slot for reuse and persists the change.
    bool confirm_removed(const core::DeviceId& device_id) noexcept;

    // Occupied slots (kAssigned + kPendingRemoval), for diagnostics/tests.
    std::size_t occupied_count() const noexcept;

private:
    struct Entry {
        bool valid{false};
        core::DeviceId device_id{};
        MatterEndpointState state{MatterEndpointState::kUnassigned};
    };

    int find_index_by_device_id(const core::DeviceId& device_id) const noexcept;
    bool save() noexcept;

    std::array<Entry, kCapacity> entries_{};
};

}  // namespace service
