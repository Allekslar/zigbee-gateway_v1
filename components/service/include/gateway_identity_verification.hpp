/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include "gateway_id.hpp"
#include "secure_storage_port.hpp"

namespace service {

// Plan S6 "HTTPS and sessions" #8's remaining half (GatewayId derivation
// itself already existed from S4): "Verify manufacturing/provisioning
// uniqueness; duplicate/cloned GatewayId evidence blocks production
// enrollment." FD-17, verbatim: "Manufacturing/provisioning evidence must
// reject duplicate or cloned GatewayId enrollment."
//
// The real fleet-wide/manufacturing-line half of this requirement --
// detecting a GatewayId reused across two physically different devices,
// or asserting a specific device was genuinely manufactured on this
// product's own line -- needs infrastructure this sandbox does not have
// (a manufacturing backend or fleet registry), and in any case depends on
// the real eFuse burn/attestation workflow (plan #6-#8, S5) that
// S5-completion.json already found BLOCKED_SECURITY_PROVISIONING (no
// manufacturing/eFuse environment exists here). This module cannot and
// does not attempt to build a substitute for either -- doing so would
// produce a check that verifies nothing real.
//
// What IS buildable and meaningful today: a LOCAL self-consistency check
// between the GatewayId this device's firmware reads live from its own
// factory base MAC (service_runtime.cpp's gateway_id() accessor) and the
// GatewayId recorded once during manufacturing in encrypted storage
// (get_stored_manufacturing_gateway_id() below). A mismatch is real,
// concrete evidence of exactly the failure mode this plan's own FD-17
// text names elsewhere (the HA-discovery footnote: "cloning the firmware
// image to different hardware therefore yields a different gateway
// identity") -- this device's current hardware does not match what was
// recorded when it was manufactured. This is explicitly NOT the same
// guarantee as fleet-wide duplicate-enrollment detection (a single
// device cannot observe any other device at all) -- documented as such,
// not overclaimed.
//
// Fails closed on every non-match outcome: no manufacturing record is
// treated exactly the same as a mismatched one -- absence of
// manufacturing evidence is never implicit proof of authenticity. Since
// no real manufacturing workflow populates this record yet
// (BLOCKED_SECURITY_PROVISIONING, same reason as above),
// gateway_id_verification_allows_production_enrollment() always returns
// false in a real deployment today -- an expected, documented
// consequence, not a defect, mirroring provisioning_secret_provider.hpp's
// own production adapter (which also always fails closed today for the
// identical missing-manufacturing-material reason).
//
// Nothing in this repository calls any function below yet -- the real
// consumer is plan #17's `POST /api/v1/provisioning/enroll` route, a
// separate, not-yet-implemented S6 sub-slice. Matches this project's
// established "port/state-machine defined ahead of its full pipeline"
// precedent.

enum class GatewayIdVerificationResult : uint8_t {
    // A manufacturing record exists and exactly matches the live-read
    // GatewayId.
    kVerified = 0,
    // A manufacturing record exists but differs from the live-read
    // GatewayId -- concrete cloned/relocated-firmware evidence.
    kMismatch = 1,
    // No manufacturing record is stored yet (SecureStorageStatus other
    // than kAvailable) -- today's reality for every real deployment,
    // pending plan #6-#8's own real manufacturing/eFuse workflow.
    kNoManufacturingRecord = 2,
};

// Typed storage accessors over secure_storage_get_blob()/set_blob() (S5
// Section 2.8/2.9), scoped to NvsNamespaceId::kManufacturingProvisioning's
// "mfg_gateway_id" key -- the raw 6-byte GatewayId, deliberately NOT the
// full eFuse-provisioning-template JSON record
// (manufacturing_provisioning_get_efuse_record(), plan S5 #5,
// tls_provisioning_storage_port.hpp) which this module does not parse
// on-device (parsing JSON here ahead of plan #24's not-yet-built
// cJSON-backed StrictJsonObjectReader would duplicate that future work,
// which this project's own FD text forbids without a documented unmet
// product requirement).
SecureStorageStatus get_stored_manufacturing_gateway_id(common::GatewayId* out) noexcept;
SecureStorageWriteResult set_stored_manufacturing_gateway_id(const common::GatewayId& gateway_id) noexcept;

// Compares `live_gateway_id` against the recorded manufacturing
// GatewayId. `live_gateway_id` itself is not independently re-validated
// for GatewayId::valid() here -- that is the caller's own responsibility
// (matching how build_provisioning_ap_ssid()/build_gateway_mdns_host()
// each independently validate their own inputs rather than this module
// duplicating that check).
GatewayIdVerificationResult verify_gateway_id_against_manufacturing_record(
    const common::GatewayId& live_gateway_id) noexcept;

// The single fail-closed predicate a future production-enrollment call
// site should consult -- true only for
// GatewayIdVerificationResult::kVerified.
constexpr bool gateway_id_verification_allows_production_enrollment(GatewayIdVerificationResult result) noexcept {
    return result == GatewayIdVerificationResult::kVerified;
}

}  // namespace service
