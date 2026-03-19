/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "ota_manifest.hpp"

namespace {

service::OtaManifestContext make_context() {
    service::OtaManifestContext context{};
    std::strncpy(context.current_version.data(), "2.4.0", context.current_version.size() - 1U);
    std::strncpy(context.current_project.data(), "zigbee-gateway", context.current_project.size() - 1U);
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

}  // namespace

int main() {
    const service::OtaManifestContext context = make_context();

    service::OtaManifest defaults = make_manifest("https://updates.local/gateway.bin");
    service::apply_ota_manifest_defaults(context, &defaults);
    assert(std::strcmp(defaults.project.data(), "zigbee-gateway") == 0);
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

    return 0;
}
