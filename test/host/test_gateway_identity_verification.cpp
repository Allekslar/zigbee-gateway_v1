/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <array>
#include <cassert>

#include "gateway_identity_verification.hpp"
#include "hal_security_state_test.h"

// get_stored_manufacturing_gateway_id() shares hal_nvs.c's single static
// in-process host store with every other test file that touches
// NvsNamespaceId::kManufacturingProvisioning -- but each test executable
// is its own process, so no cross-file ordering concern. Within this
// file, the "not provisioned yet" tests must run before any test that
// writes a record.

namespace {

using service::GatewayIdVerificationResult;
using service::SecureStorageStatus;
using service::SecureStorageWriteResult;

common::GatewayId make_gateway_id(uint8_t fill_byte) {
    std::array<uint8_t, common::GatewayId::kByteLength> bytes{};
    bytes.fill(fill_byte);
    return common::GatewayId(bytes);
}

void test_get_stored_manufacturing_gateway_id_is_not_provisioned_before_any_write() {
    common::GatewayId out{};
    assert(service::get_stored_manufacturing_gateway_id(&out) == SecureStorageStatus::kNotProvisioned);
}

void test_verify_gateway_id_reports_no_manufacturing_record_before_any_write() {
    const common::GatewayId live_id = make_gateway_id(0xAB);
    assert(
        service::verify_gateway_id_against_manufacturing_record(live_id) ==
        GatewayIdVerificationResult::kNoManufacturingRecord);
}

void test_no_manufacturing_record_never_allows_production_enrollment() {
    assert(!service::gateway_id_verification_allows_production_enrollment(
        GatewayIdVerificationResult::kNoManufacturingRecord));
}

void test_get_stored_manufacturing_gateway_id_rejects_null_out() {
    assert(service::get_stored_manufacturing_gateway_id(nullptr) == SecureStorageStatus::kUnavailable);
}

void test_set_stored_manufacturing_gateway_id_rejected_when_encryption_not_verified() {
    hal_security_state_reset_mock_flash_encryption_enabled();  // false -- "not verified"

    const common::GatewayId gateway_id = make_gateway_id(0xCD);
    assert(
        service::set_stored_manufacturing_gateway_id(gateway_id) ==
        SecureStorageWriteResult::kRejectedEncryptionNotVerified);
}

void test_set_and_get_stored_manufacturing_gateway_id_round_trip() {
    hal_security_state_set_mock_flash_encryption_enabled(true);

    const common::GatewayId gateway_id = make_gateway_id(0x11);
    assert(service::set_stored_manufacturing_gateway_id(gateway_id) == SecureStorageWriteResult::kWritten);

    common::GatewayId read_back{};
    assert(service::get_stored_manufacturing_gateway_id(&read_back) == SecureStorageStatus::kAvailable);
    assert(read_back == gateway_id);

    hal_security_state_reset_mock_flash_encryption_enabled();
}

void test_verify_gateway_id_matches_returns_verified() {
    hal_security_state_set_mock_flash_encryption_enabled(true);

    const common::GatewayId gateway_id = make_gateway_id(0x22);
    assert(service::set_stored_manufacturing_gateway_id(gateway_id) == SecureStorageWriteResult::kWritten);

    assert(
        service::verify_gateway_id_against_manufacturing_record(gateway_id) == GatewayIdVerificationResult::kVerified);
    assert(service::gateway_id_verification_allows_production_enrollment(GatewayIdVerificationResult::kVerified));

    hal_security_state_reset_mock_flash_encryption_enabled();
}

void test_verify_gateway_id_mismatch_is_rejected() {
    hal_security_state_set_mock_flash_encryption_enabled(true);

    const common::GatewayId recorded_gateway_id = make_gateway_id(0x33);
    assert(service::set_stored_manufacturing_gateway_id(recorded_gateway_id) == SecureStorageWriteResult::kWritten);

    const common::GatewayId different_live_gateway_id = make_gateway_id(0x44);
    const GatewayIdVerificationResult result =
        service::verify_gateway_id_against_manufacturing_record(different_live_gateway_id);
    assert(result == GatewayIdVerificationResult::kMismatch);
    assert(!service::gateway_id_verification_allows_production_enrollment(result));

    hal_security_state_reset_mock_flash_encryption_enabled();
}

}  // namespace

int main() {
    test_get_stored_manufacturing_gateway_id_is_not_provisioned_before_any_write();
    test_verify_gateway_id_reports_no_manufacturing_record_before_any_write();
    test_no_manufacturing_record_never_allows_production_enrollment();
    test_get_stored_manufacturing_gateway_id_rejects_null_out();
    test_set_stored_manufacturing_gateway_id_rejected_when_encryption_not_verified();
    test_set_and_get_stored_manufacturing_gateway_id_round_trip();
    test_verify_gateway_id_matches_returns_verified();
    test_verify_gateway_id_mismatch_is_rejected();
    return 0;
}
