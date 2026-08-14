/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "gateway_identity_verification.hpp"

#include <array>
#include <cstring>

#include "nvs_namespace_registry.hpp"

namespace service {

namespace {

constexpr const char* kManufacturingGatewayIdKey = "mfg_gateway_id";

}  // namespace

SecureStorageStatus get_stored_manufacturing_gateway_id(common::GatewayId* out) noexcept {
    if (out == nullptr) {
        return SecureStorageStatus::kUnavailable;
    }

    uint8_t bytes[common::GatewayId::kByteLength];
    uint32_t bytes_len = 0U;
    const SecureStorageStatus status = secure_storage_get_blob(
        NvsNamespaceId::kManufacturingProvisioning, kManufacturingGatewayIdKey, bytes, sizeof(bytes), &bytes_len);

    const bool content_valid = status == SecureStorageStatus::kAvailable && bytes_len == sizeof(bytes);
    const SecureStorageStatus classified = secure_storage_downgrade_to_corrupt_if_invalid(status, content_valid);
    if (classified != SecureStorageStatus::kAvailable) {
        return classified;
    }

    std::array<uint8_t, common::GatewayId::kByteLength> gateway_id_bytes{};
    std::memcpy(gateway_id_bytes.data(), bytes, sizeof(bytes));
    *out = common::GatewayId(gateway_id_bytes);
    return SecureStorageStatus::kAvailable;
}

SecureStorageWriteResult set_stored_manufacturing_gateway_id(const common::GatewayId& gateway_id) noexcept {
    return secure_storage_set_blob(
        NvsNamespaceId::kManufacturingProvisioning, kManufacturingGatewayIdKey, gateway_id.bytes().data(),
        static_cast<uint32_t>(common::GatewayId::kByteLength));
}

GatewayIdVerificationResult verify_gateway_id_against_manufacturing_record(
    const common::GatewayId& live_gateway_id) noexcept {
    common::GatewayId recorded_gateway_id{};
    if (get_stored_manufacturing_gateway_id(&recorded_gateway_id) != SecureStorageStatus::kAvailable) {
        return GatewayIdVerificationResult::kNoManufacturingRecord;
    }

    if (recorded_gateway_id != live_gateway_id) {
        return GatewayIdVerificationResult::kMismatch;
    }
    return GatewayIdVerificationResult::kVerified;
}

}  // namespace service
