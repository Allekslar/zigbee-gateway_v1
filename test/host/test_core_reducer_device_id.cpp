/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

// Focused coverage for the DeviceId-primary reducer path introduced in S2.
// test_core_reducer.cpp continues to cover general per-event-type behavior
// using the legacy short_addr-only path (still valid: Service-layer identity
// resolution wiring is a tracked follow-up, see
// docs/architecture/DEVICE_IDENTITY.md). This file proves INV-ID-01/02/03
// hold whenever a caller supplies a resolved device_id.

#include <cassert>
#include <cstddef>

#include "core_state.hpp"

namespace {

core::DeviceId make_id(const char* hex) {
    core::DeviceId id{};
    const bool ok = core::DeviceId::parse(hex, 16, &id);
    assert(ok);
    return id;
}

const core::CoreDeviceRecord* find_by_device_id(const core::CoreState& state, const core::DeviceId& id) {
    for (std::size_t i = 0; i < state.devices.size(); ++i) {
        if (state.devices[i].device_id == id) {
            return &state.devices[i];
        }
    }
    return nullptr;
}

}  // namespace

int main() {
    const core::DeviceId device_a = make_id("00124b0001aaaaaa");
    const core::DeviceId device_b = make_id("00124b0001bbbbbb");

    core::CoreState state{};

    // --- Join with a resolved identity creates one record and triggers interview ---
    core::CoreEvent joined_a{};
    joined_a.type = core::CoreEventType::kDeviceJoined;
    joined_a.device_id = device_a;
    joined_a.device_short_addr = 0x1111U;
    const core::CoreReduceResult r1 = core::core_reduce(state, joined_a);
    assert(r1.next.device_count == 1U);
    assert(r1.effects.count == 3);  // persist, telemetry, interview
    assert(r1.effects.items[2].type == core::CoreEffectType::kZigbeeInterview);

    const core::CoreDeviceRecord* rec_a = find_by_device_id(r1.next, device_a);
    assert(rec_a != nullptr);
    assert(rec_a->short_addr == 0x1111U);
    assert(rec_a->online);

    // --- INV-ID-01: same DeviceId rejoining with a NEW short_addr while still
    // online updates the SAME record's locator only; no new device, no
    // second interview cascade. ---
    core::CoreEvent remap_a{};
    remap_a.type = core::CoreEventType::kDeviceJoined;
    remap_a.device_id = device_a;
    remap_a.device_short_addr = 0x2222U;
    const core::CoreReduceResult r2 = core::core_reduce(r1.next, remap_a);
    assert(r2.next.device_count == 1U);  // still one device
    assert(r2.effects.count == 2);       // persist, telemetry only -- no re-interview

    const core::CoreDeviceRecord* rec_a_after_remap = find_by_device_id(r2.next, device_a);
    assert(rec_a_after_remap != nullptr);
    assert(rec_a_after_remap->short_addr == 0x2222U);
    assert(rec_a_after_remap->online);

    // --- INV-ID-03: a different DeviceId claiming the short_addr device_a
    // just vacated (0x1111) creates a wholly distinct record; it does not
    // inherit device_a's state (power_on/reporting_state/etc). ---
    core::CoreEvent joined_b{};
    joined_b.type = core::CoreEventType::kDeviceJoined;
    joined_b.device_id = device_b;
    joined_b.device_short_addr = 0x1111U;
    const core::CoreReduceResult r3 = core::core_reduce(r2.next, joined_b);
    assert(r3.next.device_count == 2U);  // two independent devices now

    const core::CoreDeviceRecord* rec_b = find_by_device_id(r3.next, device_b);
    assert(rec_b != nullptr);
    assert(rec_b->short_addr == 0x1111U);
    assert(!rec_b->power_on);  // fresh record, nothing inherited from device_a
    assert(rec_b->reporting_state == core::CoreReportingState::kUnknown);

    const core::CoreDeviceRecord* rec_a_still = find_by_device_id(r3.next, device_a);
    assert(rec_a_still != nullptr);
    assert(rec_a_still->short_addr == 0x2222U);  // device_a untouched by device_b's join

    // --- A resolved record can never be mutated by a legacy (unresolved
    // identity) event that happens to share its current short_addr. ---
    core::CoreEvent legacy_attr{};
    legacy_attr.type = core::CoreEventType::kAttributeReported;
    legacy_attr.device_short_addr = 0x2222U;  // device_a's current locator
    // device_id intentionally left invalid/default (legacy caller).
    legacy_attr.cluster_id = 0x0006U;
    legacy_attr.attribute_id = 0x0000U;
    legacy_attr.value_bool = true;
    const core::CoreReduceResult r4 = core::core_reduce(r3.next, legacy_attr);
    const core::CoreDeviceRecord* rec_a_untouched = find_by_device_id(r4.next, device_a);
    assert(rec_a_untouched != nullptr);
    assert(!rec_a_untouched->power_on);  // legacy event did NOT flip device_a's power state

    // --- Duplicate join for an already-online device_id at the same
    // short_addr is idempotent (no revision bump, no effects). ---
    const core::CoreReduceResult r5 = core::core_reduce(r4.next, remap_a);  // same event as before, already applied
    assert(r5.next.revision == r4.next.revision);
    assert(r5.effects.count == 0);

    // --- kDeviceLeft by DeviceId frees the record for reuse by a different DeviceId ---
    core::CoreEvent left_a{};
    left_a.type = core::CoreEventType::kDeviceLeft;
    left_a.device_id = device_a;
    const core::CoreReduceResult r6 = core::core_reduce(r5.next, left_a);
    assert(r6.next.device_count == 1U);  // only device_b remains
    assert(find_by_device_id(r6.next, device_a) == nullptr);
    assert(find_by_device_id(r6.next, device_b) != nullptr);

    return 0;
}
