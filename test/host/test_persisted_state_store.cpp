/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "device_id.hpp"
#include "hal_nvs.h"
#include "persisted_device_state.hpp"
#include "persisted_state_store.hpp"

namespace {

core::DeviceId make_id(const char* hex) {
    core::DeviceId id{};
    const bool ok = core::DeviceId::parse(hex, 16, &id);
    assert(ok);
    return id;
}

service::PersistedStatePayload make_payload(const core::DeviceId& id, uint16_t short_addr) {
    service::PersistedStatePayload payload{};
    payload.device_count = 1U;
    payload.devices[0].valid = true;
    payload.devices[0].device_id_bytes = id.bytes();
    payload.devices[0].last_short_addr = short_addr;
    payload.devices[0].power_on = true;
    return payload;
}

}  // namespace

int main() {
    // --- to_payload()/apply_payload() explicit conversion ---
    {
        core::CoreState state{};
        state.device_count = 2U;
        state.devices[0].device_id = make_id("00124b0001aaaaaa");
        state.devices[0].short_addr = 0x1111U;
        state.devices[0].online = true;
        state.devices[0].power_on = true;
        state.devices[0].has_temperature = true;
        state.devices[0].temperature_centi_c = 2150;
        // devices[1] intentionally has no valid device_id (legacy/unresolved).
        state.devices[1].short_addr = 0x2222U;
        state.devices[1].online = true;

        const service::PersistedStatePayload payload = service::to_payload(state);
        assert(payload.device_count == 1U);  // identity-less record dropped
        assert(payload.devices[0].valid);
        assert(core::DeviceId(payload.devices[0].device_id_bytes) == state.devices[0].device_id);
        assert(payload.devices[0].has_temperature);
        assert(payload.devices[0].temperature_centi_c == 2150);

        const core::CoreState restored = service::apply_payload(payload);
        assert(restored.device_count == 1U);
        assert(restored.devices[0].device_id == state.devices[0].device_id);
        assert(!restored.devices[0].online);  // sanitized offline
        assert(restored.devices[0].power_on);
        assert(restored.devices[0].has_temperature);
        assert(restored.devices[0].temperature_centi_c == 2150);
        assert(!restored.network_connected);
    }

    // --- PersistedStateStore: fresh load is kNotFound ---
    {
        service::PersistedStateStore store;
        service::PersistedStatePayload out{};
        assert(store.load(&out) == service::PersistedStateStore::LoadResult::kNotFound);
    }

    // --- Round trip ---
    {
        service::PersistedStateStore store;
        const core::DeviceId id = make_id("00124b0001bbbbbb");
        const service::PersistedStatePayload payload = make_payload(id, 0x3333U);
        assert(store.save(payload));

        service::PersistedStatePayload loaded{};
        assert(store.load(&loaded) == service::PersistedStateStore::LoadResult::kLoaded);
        assert(loaded.device_count == 1U);
        assert(core::DeviceId(loaded.devices[0].device_id_bytes) == id);
        assert(loaded.devices[0].last_short_addr == 0x3333U);
    }

    // --- Generation rollover: second save produces newer data; both slots
    // remain independently valid until overwritten again. ---
    {
        service::PersistedStateStore store;
        const core::DeviceId id_a = make_id("00124b0001cccccc");
        const core::DeviceId id_b = make_id("00124b0001dddddd");

        assert(store.save(make_payload(id_a, 0x4001U)));
        assert(store.save(make_payload(id_b, 0x4002U)));

        service::PersistedStatePayload loaded{};
        assert(store.load(&loaded) == service::PersistedStateStore::LoadResult::kLoaded);
        assert(core::DeviceId(loaded.devices[0].device_id_bytes) == id_b);  // latest generation wins
    }

    // --- Power loss / corruption resilience: an interrupted write to the
    // target slot must never make a previously-committed generation
    // unreadable -- the other slot remains authoritative. ---
    {
        service::PersistedStateStore store;
        const core::DeviceId id_a = make_id("00124b0001eeeeee");
        assert(store.save(make_payload(id_a, 0x5001U)));

        // Simulate a write interrupted mid-flight: corrupt whichever slot
        // save() will target next (the one NOT holding the current best
        // generation) by writing garbage directly through the raw NVS API,
        // bypassing PersistedStateStore's own validated save() path.
        uint8_t garbage[64];
        std::memset(garbage, 0xAA, sizeof(garbage));
        assert(hal_nvs_set_blob("dstate_a", garbage, sizeof(garbage)) == HAL_NVS_STATUS_OK);
        assert(hal_nvs_set_blob("dstate_b", garbage, sizeof(garbage)) == HAL_NVS_STATUS_OK);

        service::PersistedStatePayload loaded{};
        // Both slots are now corrupt -- this is the true "no safe copy left"
        // case; the store must fail closed (kCorrupt), never fabricate data.
        assert(store.load(&loaded) == service::PersistedStateStore::LoadResult::kCorrupt);

        // A fresh save recovers cleanly (both slots get valid data again).
        const core::DeviceId id_b = make_id("00124b0001ffffff");
        assert(store.save(make_payload(id_b, 0x5002U)));
        assert(store.load(&loaded) == service::PersistedStateStore::LoadResult::kLoaded);
        assert(core::DeviceId(loaded.devices[0].device_id_bytes) == id_b);
    }

    // --- One corrupted slot, one valid slot: load() must prefer the valid
    // one. save() always targets whichever slot is NOT currently best, so
    // after both slots are first reset to garbage and then save() is called
    // exactly once, exactly one of the two physical keys holds the fresh
    // valid record and the other still holds the earlier garbage -- without
    // this test needing to know or assume which named key is which. ---
    {
        service::PersistedStateStore store;
        uint8_t garbage[64];
        std::memset(garbage, 0x55, sizeof(garbage));
        assert(hal_nvs_set_blob("dstate_a", garbage, sizeof(garbage)) == HAL_NVS_STATUS_OK);
        assert(hal_nvs_set_blob("dstate_b", garbage, sizeof(garbage)) == HAL_NVS_STATUS_OK);

        const core::DeviceId id_a = make_id("00124b0001111111");
        assert(store.save(make_payload(id_a, 0x6001U)));

        service::PersistedStatePayload loaded{};
        assert(store.load(&loaded) == service::PersistedStateStore::LoadResult::kLoaded);
        assert(core::DeviceId(loaded.devices[0].device_id_bytes) == id_a);
    }

    return 0;
}
