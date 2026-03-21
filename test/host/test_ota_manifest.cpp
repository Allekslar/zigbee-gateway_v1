/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "hal_ota.h"
#include "ota_manifest.hpp"

namespace {

char g_last_signature_payload[service::kOtaManifestSigningPayloadMaxLen]{};
char g_expected_signature[service::kOtaManifestSignatureMaxLen]{};
char g_expected_signature_algo[service::kOtaManifestSignatureAlgoMaxLen]{};
char g_expected_signature_key_id[service::kOtaManifestSignatureKeyIdMaxLen]{};

service::OtaManifestContext make_context() {
    service::OtaManifestContext context{};
    std::strncpy(context.current_version.data(), "2.4.0", context.current_version.size() - 1U);
    std::strncpy(context.current_project.data(), "zigbee_gateway", context.current_project.size() - 1U);
    std::strncpy(context.current_board.data(), "zigbee-gateway-esp32c6", context.current_board.size() - 1U);
    std::strncpy(context.current_chip_target.data(), "esp32c6", context.current_chip_target.size() - 1U);
    context.current_schema = 3U;
    return context;
}

service::OtaManifest make_manifest(const char* url) {
    service::OtaManifest manifest{};
    if (url != nullptr) {
        std::strncpy(manifest.url.data(), url, manifest.url.size() - 1U);
    }
    return manifest;
}

extern "C" bool hal_ota_platform_verify_manifest_signature(
    const char* payload,
    size_t payload_len,
    const char* signature_algo,
    const char* signature_key_id,
    const char* signature_hex) {
    assert(payload != nullptr);
    assert(signature_algo != nullptr);
    assert(signature_key_id != nullptr);
    assert(signature_hex != nullptr);
    std::memset(g_last_signature_payload, 0, sizeof(g_last_signature_payload));
    std::strncpy(g_last_signature_payload, payload, payload_len < sizeof(g_last_signature_payload) ? payload_len : (sizeof(g_last_signature_payload) - 1U));
    return std::strcmp(signature_algo, g_expected_signature_algo) == 0 &&
           std::strcmp(signature_key_id, g_expected_signature_key_id) == 0 &&
           std::strcmp(signature_hex, g_expected_signature) == 0;
}

}  // namespace

int main() {
    const service::OtaManifestContext context = make_context();

    service::OtaManifest defaults = make_manifest("https://updates.local/gateway.bin");
    service::apply_ota_manifest_defaults(context, &defaults);
    assert(std::strcmp(defaults.project.data(), "zigbee_gateway") == 0);
    assert(std::strcmp(defaults.board.data(), "zigbee-gateway-esp32c6") == 0);
    assert(std::strcmp(defaults.chip_target.data(), "esp32c6") == 0);
    assert(defaults.min_schema == 3U);
    assert(service::validate_ota_manifest(defaults, context) == service::OtaManifestValidationStatus::kOk);

    service::OtaManifest bad_sha = defaults;
    std::strncpy(bad_sha.sha256.data(), "xyz", bad_sha.sha256.size() - 1U);
    assert(service::validate_ota_manifest(bad_sha, context) == service::OtaManifestValidationStatus::kInvalidSha256);

    service::OtaManifest insecure_url = make_manifest("http://updates.local/gateway.bin");
    service::apply_ota_manifest_defaults(context, &insecure_url);
    assert(service::validate_ota_manifest(insecure_url, context) == service::OtaManifestValidationStatus::kInvalidUrl);

    service::OtaManifest wrong_project = defaults;
    std::strncpy(wrong_project.project.data(), "other-gateway", wrong_project.project.size() - 1U);
    assert(service::validate_ota_manifest(wrong_project, context) ==
           service::OtaManifestValidationStatus::kProjectMismatch);

    service::OtaManifest wrong_board = defaults;
    std::strncpy(wrong_board.board.data(), "zigbee-gateway-esp32s3", wrong_board.board.size() - 1U);
    assert(service::validate_ota_manifest(wrong_board, context) ==
           service::OtaManifestValidationStatus::kBoardMismatch);

    service::OtaManifest wrong_chip = defaults;
    std::strncpy(wrong_chip.chip_target.data(), "esp32s3", wrong_chip.chip_target.size() - 1U);
    assert(service::validate_ota_manifest(wrong_chip, context) ==
           service::OtaManifestValidationStatus::kChipTargetMismatch);

    service::OtaManifest schema_too_new = defaults;
    schema_too_new.min_schema = 4U;
    assert(service::validate_ota_manifest(schema_too_new, context) ==
           service::OtaManifestValidationStatus::kSchemaTooNew);

    service::OtaManifest downgrade = defaults;
    std::strncpy(downgrade.version.data(), "2.3.9", downgrade.version.size() - 1U);
    assert(service::validate_ota_manifest(downgrade, context) ==
           service::OtaManifestValidationStatus::kDowngradeRejected);

    downgrade.allow_downgrade = true;
    assert(service::validate_ota_manifest(downgrade, context) == service::OtaManifestValidationStatus::kOk);

    service::OtaManifest non_numeric_version = defaults;
    std::strncpy(non_numeric_version.version.data(), "release-candidate", non_numeric_version.version.size() - 1U);
    assert(service::validate_ota_manifest(non_numeric_version, context) == service::OtaManifestValidationStatus::kOk);

    service::OtaManifest signed_manifest = defaults;
    std::strncpy(signed_manifest.version.data(), "2.4.1", signed_manifest.version.size() - 1U);
    std::strncpy(signed_manifest.signature_algo.data(), "ecdsa-p256-sha256", signed_manifest.signature_algo.size() - 1U);
    std::strncpy(signed_manifest.signature_key_id.data(), "ota-release-v1", signed_manifest.signature_key_id.size() - 1U);
    std::strncpy(signed_manifest.signature.data(), "test-signature", signed_manifest.signature.size() - 1U);

    std::strncpy(g_expected_signature_algo, "ecdsa-p256-sha256", sizeof(g_expected_signature_algo) - 1U);
    std::strncpy(g_expected_signature_key_id, "ota-release-v1", sizeof(g_expected_signature_key_id) - 1U);
    std::strncpy(g_expected_signature, "test-signature", sizeof(g_expected_signature) - 1U);

    char expected_payload[service::kOtaManifestSigningPayloadMaxLen]{};
    assert(service::build_ota_manifest_signing_payload(signed_manifest, expected_payload, sizeof(expected_payload)));
    assert(service::validate_ota_manifest(signed_manifest, context) == service::OtaManifestValidationStatus::kOk);
    assert(std::strcmp(g_last_signature_payload, expected_payload) == 0);

    service::OtaManifest missing_signature = signed_manifest;
    missing_signature.signature[0] = '\0';
    assert(service::validate_ota_manifest(missing_signature, context) ==
           service::OtaManifestValidationStatus::kMissingSignature);

    service::OtaManifest invalid_signature = signed_manifest;
    std::strncpy(invalid_signature.signature.data(), "tampered-signature", invalid_signature.signature.size() - 1U);
    assert(service::validate_ota_manifest(invalid_signature, context) ==
           service::OtaManifestValidationStatus::kInvalidSignature);

    service::OtaManifest rotated_key_manifest = defaults;
    std::strncpy(rotated_key_manifest.version.data(), "2.4.2", rotated_key_manifest.version.size() - 1U);
    std::strncpy(rotated_key_manifest.signature_algo.data(), "ecdsa-p256-sha256", rotated_key_manifest.signature_algo.size() - 1U);
    std::strncpy(rotated_key_manifest.signature_key_id.data(), "ota-release-v2", rotated_key_manifest.signature_key_id.size() - 1U);
    std::strncpy(rotated_key_manifest.signature.data(), "next-key-signature", rotated_key_manifest.signature.size() - 1U);
    std::strncpy(g_expected_signature_key_id, "ota-release-v2", sizeof(g_expected_signature_key_id) - 1U);
    std::strncpy(g_expected_signature, "next-key-signature", sizeof(g_expected_signature) - 1U);
    assert(service::validate_ota_manifest(rotated_key_manifest, context) == service::OtaManifestValidationStatus::kOk);

    return 0;
}
