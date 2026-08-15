/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

#include "service_runtime_api.hpp"

namespace service {

// Plan S6 "Authorization and physical presence" #18: "Define capabilities:
// read_status; control_device; manage_network; commission_device;
// remove_device; firmware_admin; rcp_admin; security_admin; factory_reset."
//
// This system has exactly one administrative role -- there is no user/role
// table anywhere in this codebase, only a single AdminVerifierRecord (S6
// "Provisioning and credentials" #5). Every successfully authenticated
// session therefore holds every capability the current build actually
// supports (see granted_capabilities() below) -- there is no per-user
// capability SUBSET to check yet, and building one would check nothing
// real (the same "would verify nothing real" reasoning
// gateway_identity_verification.hpp's own header already applies to its
// own narrower self-consistency scoping). `route_authorization.hpp`'s
// authorize_v1_request() therefore does not branch on a Capability value
// at all today -- it enforces "is there a valid, authenticated session"
// (and, for mutations, CSRF+origin), which is the one real, enforceable
// thing this single-role system can check. The per-route Capability tag
// passed at each v1 handler's call site exists for two real, present-day
// purposes: (1) `GET /api/v1/auth/session`'s "actor capabilities" response
// (plan #17) -- filtered by which build capabilities are actually
// available, via granted_capabilities() below; (2) a self-describing,
// grep-able label at each call site for the not-yet-built #30/#31
// audit-record "action" field. If a real multi-role system is ever added,
// only the capability-GRANT step (who gets which Capability) would need to
// change -- the per-route requirement table already matches the plan's
// exact taxonomy today.
enum class Capability : uint8_t {
    kReadStatus = 0,
    kControlDevice = 1,
    kManageNetwork = 2,
    kCommissionDevice = 3,
    kRemoveDevice = 4,
    kFirmwareAdmin = 5,
    kRcpAdmin = 6,
    kSecurityAdmin = 7,
    kFactoryReset = 8,
};

inline constexpr uint32_t kCapabilityCount = 9U;

// Exact plan #18 token spelling (snake_case, matches the plan text
// verbatim) -- used for both the GET /api/v1/auth/session JSON response
// and (later) audit-record action labeling.
const char* capability_token(Capability capability) noexcept;

// Every capability an authenticated session holds under the current
// build, in plan #18's own declared order. `firmware_admin`/`rcp_admin`
// are withheld when the underlying subsystem isn't actually built
// (`runtime_caps.ota_available`/`rcp_update_available` false) -- an
// unavailable capability would be immediately rejected by its own route's
// pre-existing kCapabilityUnavailable check anyway (e.g. the RCP/OTA
// handlers' own capability gate, unrelated to authorization), so omitting
// it here keeps the session response honest about what this actor can
// really do on this exact device. `security_admin`/`factory_reset` are
// always listed: certificate-rotation/factory-reset ROUTES exist (or will
// -- #17) and return their own capability_unavailable today, independent
// of this list. Returns the number of capabilities written to `out`
// (capacity kCapabilityCount).
uint32_t granted_capabilities(
    const RuntimeCapabilities& runtime_caps, Capability* out, uint32_t out_capacity) noexcept;

}  // namespace service
