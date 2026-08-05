/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstdio>

#include "hal_nvs.h"
#include "matter_endpoint_registry.hpp"

namespace {

core::DeviceId make_id(const char* hex) {
    core::DeviceId id{};
    const bool ok = core::DeviceId::parse(hex, 16, &id);
    assert(ok);
    return id;
}

// The host hal_nvs mock only clears its backing storage on the very first
// hal_nvs_init() call in the process (it simulates "first boot"); every
// later call in this same binary is a no-op. Since every test function
// below shares the same two NVS keys (mtep_a/mtep_b), each one must force
// a known-clean starting point itself rather than relying on hal_nvs_init()
// -- write mismatched-length garbage so MatterEndpointRegistry::load()
// deterministically treats both slots as corrupt/absent and starts empty
// (its documented fail-safe behavior, exercised directly by
// test_corrupt_store_starts_empty_fail_safe below).
void force_clean_matter_store() {
    const uint8_t garbage[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    (void)hal_nvs_set_blob("mtep_a", garbage, sizeof(garbage));
    (void)hal_nvs_set_blob("mtep_b", garbage, sizeof(garbage));
}

void test_allocate_is_deterministic_lowest_free_and_idempotent() {
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);
    force_clean_matter_store();
    service::MatterEndpointRegistry registry;
    registry.load();

    const core::DeviceId device_a = make_id("00124b0001aaaaaa");
    const core::DeviceId device_b = make_id("00124b0001bbbbbb");

    uint8_t endpoint_a = 0U;
    assert(
        registry.allocate(device_a, &endpoint_a) == service::MatterEndpointAllocateResult::kAssigned);
    assert(endpoint_a == service::MatterEndpointRegistry::kEndpointBase);

    // Idempotent: re-allocating the same device returns the same endpoint
    // without consuming a new slot.
    uint8_t endpoint_a_again = 0U;
    assert(
        registry.allocate(device_a, &endpoint_a_again) ==
        service::MatterEndpointAllocateResult::kAlreadyAssigned);
    assert(endpoint_a_again == endpoint_a);

    // A different DeviceId gets the next lowest free slot.
    uint8_t endpoint_b = 0U;
    assert(
        registry.allocate(device_b, &endpoint_b) == service::MatterEndpointAllocateResult::kAssigned);
    assert(endpoint_b == service::MatterEndpointRegistry::kEndpointBase + 1U);
    assert(endpoint_b != endpoint_a);

    uint8_t found_a = 0U;
    assert(registry.find(device_a, &found_a));
    assert(found_a == endpoint_a);
}

void test_invalid_device_id_rejected() {
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);
    force_clean_matter_store();
    service::MatterEndpointRegistry registry;
    registry.load();

    uint8_t endpoint = 0U;
    assert(
        registry.allocate(core::DeviceId{}, &endpoint) ==
        service::MatterEndpointAllocateResult::kInvalidArgument);
    assert(!registry.find(core::DeviceId{}, &endpoint));
}

void test_capacity_exhaustion_is_explicit() {
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);
    force_clean_matter_store();
    service::MatterEndpointRegistry registry;
    registry.load();

    for (std::size_t i = 0; i < service::MatterEndpointRegistry::kCapacity; ++i) {
        char hex[17] = {};
        // Deterministic distinct 8-byte id per iteration, avoiding all-zero/all-ff.
        std::snprintf(hex, sizeof(hex), "00124b%010zx", i + 1U);
        const core::DeviceId device_id = make_id(hex);
        uint8_t endpoint = 0U;
        assert(registry.allocate(device_id, &endpoint) == service::MatterEndpointAllocateResult::kAssigned);
    }
    assert(registry.occupied_count() == service::MatterEndpointRegistry::kCapacity);

    const core::DeviceId one_too_many = make_id("00124bffffffffff");
    uint8_t endpoint = 0U;
    assert(
        registry.allocate(one_too_many, &endpoint) == service::MatterEndpointAllocateResult::kNoCapacity);
}

void test_two_phase_removal_blocks_reuse_until_confirmed() {
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);
    force_clean_matter_store();
    service::MatterEndpointRegistry registry;
    registry.load();

    const core::DeviceId device_a = make_id("00124b0001aaaaaa");
    uint8_t endpoint_a = 0U;
    assert(registry.allocate(device_a, &endpoint_a) == service::MatterEndpointAllocateResult::kAssigned);

    // Step 1: mark pending removal. The endpoint is no longer resolvable
    // via find() (it is being retired, not published)...
    uint8_t removed_endpoint = 0U;
    assert(registry.mark_pending_removal(device_a, &removed_endpoint));
    assert(removed_endpoint == endpoint_a);
    uint8_t ignored = 0U;
    assert(!registry.find(device_a, &ignored));

    // ...but the slot is NOT reusable yet: a fresh device_id must NOT be
    // able to inherit it, and the same device_id rejoining mid-removal
    // must get a genuinely new slot, never the pending one (FD-01's
    // no-state-inheritance rule extended to Matter identity).
    const core::DeviceId device_b = make_id("00124b0001bbbbbb");
    uint8_t endpoint_b = 0U;
    assert(registry.allocate(device_b, &endpoint_b) == service::MatterEndpointAllocateResult::kAssigned);
    assert(endpoint_b != endpoint_a);

    uint8_t rejoin_endpoint = 0U;
    assert(
        registry.allocate(device_a, &rejoin_endpoint) == service::MatterEndpointAllocateResult::kAssigned);
    assert(rejoin_endpoint != endpoint_a);
    assert(rejoin_endpoint != endpoint_b);

    // Step 2: confirm removal of the ORIGINAL (still-pending) entry frees
    // its slot for future allocation.
    assert(registry.confirm_removed(device_a));
    // confirm_removed only applies to the specific pending entry; the
    // rejoin above created a *new* kAssigned entry for device_a in a
    // different slot, which confirm_removed must not have touched.
    uint8_t rejoin_still_assigned = 0U;
    assert(registry.find(device_a, &rejoin_still_assigned));
    assert(rejoin_still_assigned == rejoin_endpoint);
}

void test_persists_across_reload_reboot_simulation() {
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);
    force_clean_matter_store();
    const core::DeviceId device_a = make_id("00124b0001aaaaaa");
    uint8_t endpoint_a = 0U;

    {
        service::MatterEndpointRegistry writer;
        writer.load();
        assert(
            writer.allocate(device_a, &endpoint_a) == service::MatterEndpointAllocateResult::kAssigned);
    }

    // Fresh instance, same underlying NVS -- simulates a reboot.
    service::MatterEndpointRegistry reader;
    reader.load();
    uint8_t reloaded_endpoint = 0U;
    assert(reader.find(device_a, &reloaded_endpoint));
    assert(reloaded_endpoint == endpoint_a);
}

void test_pending_removal_state_survives_reload() {
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);
    force_clean_matter_store();
    const core::DeviceId device_a = make_id("00124b0001aaaaaa");

    {
        service::MatterEndpointRegistry writer;
        writer.load();
        uint8_t endpoint = 0U;
        assert(writer.allocate(device_a, &endpoint) == service::MatterEndpointAllocateResult::kAssigned);
        uint8_t removed_endpoint = 0U;
        assert(writer.mark_pending_removal(device_a, &removed_endpoint));
    }

    // Reboot before tombstone confirmation: the slot must remain
    // non-reusable (still pending), not silently revert to unassigned.
    service::MatterEndpointRegistry reader;
    reader.load();
    uint8_t ignored = 0U;
    assert(!reader.find(device_a, &ignored));
    assert(reader.occupied_count() == 1U);  // still occupied (pending), not freed.
}

void test_corrupt_store_starts_empty_fail_safe() {
    assert(hal_nvs_init() == HAL_NVS_STATUS_OK);
    // Write garbage into both slots directly (same trick force_clean_matter_
    // store() uses elsewhere in this file, exercised here as the thing
    // under test rather than as setup).
    force_clean_matter_store();

    service::MatterEndpointRegistry registry;
    registry.load();
    assert(registry.occupied_count() == 0U);

    // Still fully functional afterward.
    const core::DeviceId device_a = make_id("00124b0001aaaaaa");
    uint8_t endpoint = 0U;
    assert(registry.allocate(device_a, &endpoint) == service::MatterEndpointAllocateResult::kAssigned);
}

}  // namespace

int main() {
    test_allocate_is_deterministic_lowest_free_and_idempotent();
    test_invalid_device_id_rejected();
    test_capacity_exhaustion_is_explicit();
    test_two_phase_removal_blocks_reuse_until_confirmed();
    test_persists_across_reload_reboot_simulation();
    test_pending_removal_state_survives_reload();
    test_corrupt_store_starts_empty_fail_safe();
    return 0;
}
