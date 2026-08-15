/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "physical_presence_grant.hpp"

namespace {

constexpr const char* kSessionA = "0123456789abcdef0123456789abcdef";
constexpr const char* kSessionB = "fedcba9876543210fedcba9876543210";

}  // namespace

int main() {
    // A freshly-constructed (boot-fresh) state has no active grant --
    // plan #21's "bound to gateway boot" satisfied for free by RAM-only
    // storage.
    {
        service::PhysicalPresenceGrantState state{};
        assert(!service::physical_presence_grant_is_valid(
            state, service::PhysicalPresenceActionClass::kJoinDevice, kSessionA, 1000ULL));
        assert(!service::physical_presence_grant_consume(
            &state, service::PhysicalPresenceActionClass::kJoinDevice, kSessionA, 1000ULL));
    }

    // Create + valid immediately, for the exact action class and session
    // it was created for.
    {
        service::PhysicalPresenceGrantState state{};
        service::physical_presence_grant_create(
            &state, service::PhysicalPresenceActionClass::kRemoveDevice, kSessionA, 1000ULL);
        assert(service::physical_presence_grant_is_valid(
            state, service::PhysicalPresenceActionClass::kRemoveDevice, kSessionA, 1000ULL));
    }

    // Wrong action class -> invalid.
    {
        service::PhysicalPresenceGrantState state{};
        service::physical_presence_grant_create(
            &state, service::PhysicalPresenceActionClass::kOta, kSessionA, 1000ULL);
        assert(!service::physical_presence_grant_is_valid(
            state, service::PhysicalPresenceActionClass::kRcpUpdate, kSessionA, 1000ULL));
    }

    // Wrong session -> invalid.
    {
        service::PhysicalPresenceGrantState state{};
        service::physical_presence_grant_create(
            &state, service::PhysicalPresenceActionClass::kFactoryReset, kSessionA, 1000ULL);
        assert(!service::physical_presence_grant_is_valid(
            state, service::PhysicalPresenceActionClass::kFactoryReset, kSessionB, 1000ULL));
    }

    // A session-less grant (nullptr at creation) matches only a
    // session-less check, not any real session -- and vice versa.
    {
        service::PhysicalPresenceGrantState state{};
        service::physical_presence_grant_create(
            &state, service::PhysicalPresenceActionClass::kWifiCredentialReplacement, nullptr, 1000ULL);
        assert(service::physical_presence_grant_is_valid(
            state, service::PhysicalPresenceActionClass::kWifiCredentialReplacement, nullptr, 1000ULL));
        assert(service::physical_presence_grant_is_valid(
            state, service::PhysicalPresenceActionClass::kWifiCredentialReplacement, "", 1000ULL));
        assert(!service::physical_presence_grant_is_valid(
            state, service::PhysicalPresenceActionClass::kWifiCredentialReplacement, kSessionA, 1000ULL));
    }

    // Exactly at and past the 60-second boundary.
    {
        service::PhysicalPresenceGrantState state{};
        service::physical_presence_grant_create(
            &state, service::PhysicalPresenceActionClass::kCertificateRotation, kSessionA, 1000ULL);
        const uint64_t just_before_deadline =
            1000ULL + (static_cast<uint64_t>(service::kPhysicalPresenceGrantMaxLifetimeSeconds) * 1000ULL) - 1ULL;
        assert(service::physical_presence_grant_is_valid(
            state, service::PhysicalPresenceActionClass::kCertificateRotation, kSessionA, just_before_deadline));
        const uint64_t at_deadline =
            1000ULL + (static_cast<uint64_t>(service::kPhysicalPresenceGrantMaxLifetimeSeconds) * 1000ULL);
        assert(!service::physical_presence_grant_is_valid(
            state, service::PhysicalPresenceActionClass::kCertificateRotation, kSessionA, at_deadline));
    }

    // Clock-goes-backward guard: now_ms before created_at_ms is treated
    // as expired, not as a huge (underflowed) elapsed duration.
    {
        service::PhysicalPresenceGrantState state{};
        service::physical_presence_grant_create(
            &state, service::PhysicalPresenceActionClass::kJoinDevice, kSessionA, 5000ULL);
        assert(!service::physical_presence_grant_is_valid(
            state, service::PhysicalPresenceActionClass::kJoinDevice, kSessionA, 4999ULL));
    }

    // consume() is one-time-use: the first call succeeds and deactivates
    // the grant; an identical immediate second call fails.
    {
        service::PhysicalPresenceGrantState state{};
        service::physical_presence_grant_create(
            &state, service::PhysicalPresenceActionClass::kRcpUpdate, kSessionA, 1000ULL);
        assert(service::physical_presence_grant_consume(
            &state, service::PhysicalPresenceActionClass::kRcpUpdate, kSessionA, 1000ULL));
        assert(!service::physical_presence_grant_consume(
            &state, service::PhysicalPresenceActionClass::kRcpUpdate, kSessionA, 1000ULL));
        assert(!service::physical_presence_grant_is_valid(
            state, service::PhysicalPresenceActionClass::kRcpUpdate, kSessionA, 1000ULL));
    }

    // A failed consume() (wrong class) does not touch the still-valid
    // grant -- it can still be consumed correctly afterward.
    {
        service::PhysicalPresenceGrantState state{};
        service::physical_presence_grant_create(
            &state, service::PhysicalPresenceActionClass::kFactoryReset, kSessionA, 1000ULL);
        assert(!service::physical_presence_grant_consume(
            &state, service::PhysicalPresenceActionClass::kJoinDevice, kSessionA, 1000ULL));
        assert(service::physical_presence_grant_consume(
            &state, service::PhysicalPresenceActionClass::kFactoryReset, kSessionA, 1000ULL));
    }

    // A second create() replaces (does not stack with) an earlier grant
    // -- mirrors commissioning_window_start()'s own idempotent-restart
    // precedent.
    {
        service::PhysicalPresenceGrantState state{};
        service::physical_presence_grant_create(
            &state, service::PhysicalPresenceActionClass::kJoinDevice, kSessionA, 1000ULL);
        service::physical_presence_grant_create(
            &state, service::PhysicalPresenceActionClass::kRemoveDevice, kSessionB, 2000ULL);
        assert(!service::physical_presence_grant_is_valid(
            state, service::PhysicalPresenceActionClass::kJoinDevice, kSessionA, 2000ULL));
        assert(service::physical_presence_grant_is_valid(
            state, service::PhysicalPresenceActionClass::kRemoveDevice, kSessionB, 2000ULL));
    }

    // Null state pointer: rejected, not a crash.
    assert(!service::physical_presence_grant_is_valid(
        service::PhysicalPresenceGrantState{}, service::PhysicalPresenceActionClass::kJoinDevice, kSessionA,
        1000ULL));
    assert(!service::physical_presence_grant_consume(
        nullptr, service::PhysicalPresenceActionClass::kJoinDevice, kSessionA, 1000ULL));
    service::physical_presence_grant_create(
        nullptr, service::PhysicalPresenceActionClass::kJoinDevice, kSessionA, 1000ULL);  // no-op, must not crash

    // --- physical_presence_grant_create_from_button(): ambient grant,
    // matches ANY action class and ANY session (including none). ---
    {
        service::PhysicalPresenceGrantState state{};
        service::physical_presence_grant_create_from_button(&state, 1000ULL);
        assert(service::physical_presence_grant_is_valid(
            state, service::PhysicalPresenceActionClass::kJoinDevice, kSessionA, 1000ULL));
        assert(service::physical_presence_grant_is_valid(
            state, service::PhysicalPresenceActionClass::kFactoryReset, kSessionB, 1000ULL));
        assert(service::physical_presence_grant_is_valid(
            state, service::PhysicalPresenceActionClass::kProvisioningEnroll, nullptr, 1000ULL));

        // Still respects the 60-second lifetime and the clock-goes-
        // backward guard, exactly like a class/session-bound grant.
        const uint64_t after_deadline =
            1000ULL + (static_cast<uint64_t>(service::kPhysicalPresenceGrantMaxLifetimeSeconds) * 1000ULL);
        assert(!service::physical_presence_grant_is_valid(
            state, service::PhysicalPresenceActionClass::kJoinDevice, kSessionA, after_deadline));
        assert(!service::physical_presence_grant_is_valid(
            state, service::PhysicalPresenceActionClass::kJoinDevice, kSessionA, 999ULL));

        // Consumed once, by whichever action class/session gets there
        // first -- a second, different action class cannot also consume
        // the same ambient grant.
        assert(service::physical_presence_grant_consume(
            &state, service::PhysicalPresenceActionClass::kCertificateRotation, kSessionA, 1000ULL));
        assert(!service::physical_presence_grant_consume(
            &state, service::PhysicalPresenceActionClass::kOta, kSessionB, 1000ULL));
    }

    // An ambient grant replaces a class/session-bound one and vice versa
    // -- create()/create_from_button() always fully overwrite prior
    // state, never merge.
    {
        service::PhysicalPresenceGrantState state{};
        service::physical_presence_grant_create(
            &state, service::PhysicalPresenceActionClass::kJoinDevice, kSessionA, 1000ULL);
        service::physical_presence_grant_create_from_button(&state, 1000ULL);
        assert(service::physical_presence_grant_is_valid(
            state, service::PhysicalPresenceActionClass::kRemoveDevice, kSessionB, 1000ULL));

        service::physical_presence_grant_create(
            &state, service::PhysicalPresenceActionClass::kOta, kSessionA, 1000ULL);
        assert(!service::physical_presence_grant_is_valid(
            state, service::PhysicalPresenceActionClass::kRemoveDevice, kSessionB, 1000ULL));
        assert(service::physical_presence_grant_is_valid(
            state, service::PhysicalPresenceActionClass::kOta, kSessionA, 1000ULL));
    }

    // Null state pointer: rejected, not a crash.
    service::physical_presence_grant_create_from_button(nullptr, 1000ULL);

    return 0;
}
