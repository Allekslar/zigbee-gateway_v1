/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

#include "persisted_device_state.hpp"

namespace service {

// Generic two-generation write/validate/commit protocol (plan Section 7.3)
// over two NVS blob slots. `generation` in the header is the sole commit
// marker: load() always prefers the highest-generation slot that also
// passes magic/schema/length/CRC32C validation, so a write interrupted by
// power loss can never be mistaken for authoritative -- the other,
// previously-committed slot remains valid and is selected instead.
class PersistedStateStore {
public:
    enum class LoadResult : uint8_t {
        kLoaded,
        kNotFound,
        kCorrupt,
    };

    LoadResult load(PersistedStatePayload* out) const noexcept;

    // Writes to whichever slot does NOT currently hold the highest valid
    // generation, then reads it back and validates before returning true.
    // On failure, neither the target slot's write is trusted nor is the
    // previously-valid slot touched -- load() continues to return the old
    // generation.
    bool save(const PersistedStatePayload& payload) noexcept;

private:
    struct SlotProbe {
        bool valid{false};
        uint32_t generation{0};
    };

    SlotProbe probe_slot(const char* key) const noexcept;
};

}  // namespace service
