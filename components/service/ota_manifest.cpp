/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "ota_manifest.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <inttypes.h>

#include "hal_ota.h"

namespace service {

namespace {

bool has_prefix(const char* value, const char* prefix) noexcept {
    if (value == nullptr || prefix == nullptr) {
        return false;
    }

    const std::size_t prefix_len = std::strlen(prefix);
    return std::strncmp(value, prefix, prefix_len) == 0;
}

template <std::size_t N>
bool is_empty(const std::array<char, N>& value) noexcept {
    return value[0] == '\0';
}

template <std::size_t N>
void copy_if_empty(const std::array<char, N>& source, std::array<char, N>* target) noexcept {
    if (target == nullptr || !is_empty(*target)) {
        return;
    }

    *target = source;
}

bool strings_equal(const char* lhs, const char* rhs) noexcept {
    if (lhs == nullptr || rhs == nullptr) {
        return false;
    }
    return std::strcmp(lhs, rhs) == 0;
}

bool is_valid_sha256_hex(const char* value) noexcept {
    if (value == nullptr || *value == '\0') {
        return true;
    }

    const std::size_t length = std::strlen(value);
    if (length != kOtaManifestSha256HexLen) {
        return false;
    }

    for (std::size_t i = 0; i < length; ++i) {
        if (std::isxdigit(static_cast<unsigned char>(value[i])) == 0) {
            return false;
        }
    }
    return true;
}

bool is_empty_cstr(const char* value) noexcept {
    return value == nullptr || *value == '\0';
}

bool has_any_signature_material(const OtaManifest& manifest) noexcept {
    return !is_empty_cstr(manifest.signature_algo.data()) ||
           !is_empty_cstr(manifest.signature_key_id.data()) ||
           !is_empty_cstr(manifest.signature.data());
}

bool has_complete_signature_material(const OtaManifest& manifest) noexcept {
    return !is_empty_cstr(manifest.signature_algo.data()) &&
           !is_empty_cstr(manifest.signature_key_id.data()) &&
           !is_empty_cstr(manifest.signature.data());
}

bool parse_dotted_numeric_version(const char* value, uint32_t* out_parts, std::size_t part_capacity, std::size_t* out_count) noexcept {
    if (value == nullptr || out_parts == nullptr || out_count == nullptr || part_capacity == 0U || *value == '\0') {
        return false;
    }

    std::size_t count = 0U;
    const char* cursor = value;
    while (*cursor != '\0') {
        if (count >= part_capacity) {
            return false;
        }

        if (std::isdigit(static_cast<unsigned char>(*cursor)) == 0) {
            return false;
        }

        uint32_t part = 0U;
        while (*cursor != '\0' && *cursor != '.') {
            if (std::isdigit(static_cast<unsigned char>(*cursor)) == 0) {
                return false;
            }

            part = (part * 10U) + static_cast<uint32_t>(*cursor - '0');
            ++cursor;
        }

        out_parts[count++] = part;
        if (*cursor == '.') {
            ++cursor;
            if (*cursor == '\0') {
                return false;
            }
        }
    }

    *out_count = count;
    return count != 0U;
}

bool is_downgrade(const char* requested, const char* current) noexcept {
    uint32_t requested_parts[4]{};
    uint32_t current_parts[4]{};
    std::size_t requested_count = 0U;
    std::size_t current_count = 0U;

    if (!parse_dotted_numeric_version(requested, requested_parts, 4U, &requested_count) ||
        !parse_dotted_numeric_version(current, current_parts, 4U, &current_count)) {
        return false;
    }

    const std::size_t count = requested_count > current_count ? requested_count : current_count;
    for (std::size_t index = 0U; index < count; ++index) {
        const uint32_t lhs = index < requested_count ? requested_parts[index] : 0U;
        const uint32_t rhs = index < current_count ? current_parts[index] : 0U;
        if (lhs < rhs) {
            return true;
        }
        if (lhs > rhs) {
            return false;
        }
    }

    return false;
}

}  // namespace

void apply_ota_manifest_defaults(const OtaManifestContext& context, OtaManifest* manifest) noexcept {
    if (manifest == nullptr) {
        return;
    }

    copy_if_empty(context.current_project, &manifest->project);
    copy_if_empty(context.current_board, &manifest->board);
    copy_if_empty(context.current_chip_target, &manifest->chip_target);
    if (manifest->min_schema == 0U) {
        manifest->min_schema = context.current_schema;
    }
}

bool build_ota_manifest_signing_payload(
    const OtaManifest& manifest,
    char* out,
    std::size_t out_capacity) noexcept {
    if (out == nullptr || out_capacity == 0U) {
        return false;
    }

    const int written = std::snprintf(
        out,
        out_capacity,
        "version=%s\n"
        "url=%s\n"
        "sha256=%s\n"
        "project=%s\n"
        "board=%s\n"
        "chip_target=%s\n"
        "min_schema=%" PRIu32 "\n"
        "allow_downgrade=%s\n",
        manifest.version.data(),
        manifest.url.data(),
        manifest.sha256.data(),
        manifest.project.data(),
        manifest.board.data(),
        manifest.chip_target.data(),
        manifest.min_schema,
        manifest.allow_downgrade ? "true" : "false");

    return written > 0 && static_cast<std::size_t>(written) < out_capacity;
}

OtaManifestValidationStatus validate_ota_manifest(
    const OtaManifest& manifest,
    const OtaManifestContext& context) noexcept {
    if (manifest.url[0] == '\0') {
        return OtaManifestValidationStatus::kInvalidUrl;
    }

    if (!has_prefix(manifest.url.data(), "https://")) {
#if defined(CONFIG_ZGW_OTA_ALLOW_HTTP_URLS_FOR_TESTING) && CONFIG_ZGW_OTA_ALLOW_HTTP_URLS_FOR_TESTING
        if (!has_prefix(manifest.url.data(), "http://")) {
            return OtaManifestValidationStatus::kInvalidUrl;
        }
#else
        return OtaManifestValidationStatus::kInvalidUrl;
#endif
    }

    if (!is_valid_sha256_hex(manifest.sha256.data())) {
        return OtaManifestValidationStatus::kInvalidSha256;
    }

    if (!is_empty(manifest.project) && !strings_equal(manifest.project.data(), context.current_project.data())) {
        return OtaManifestValidationStatus::kProjectMismatch;
    }

    if (!is_empty(manifest.board) && !strings_equal(manifest.board.data(), context.current_board.data())) {
        return OtaManifestValidationStatus::kBoardMismatch;
    }

    if (!is_empty(manifest.chip_target) && !strings_equal(manifest.chip_target.data(), context.current_chip_target.data())) {
        return OtaManifestValidationStatus::kChipTargetMismatch;
    }

    if (context.current_schema != 0U && manifest.min_schema > context.current_schema) {
        return OtaManifestValidationStatus::kSchemaTooNew;
    }

    if (!manifest.allow_downgrade && manifest.version[0] != '\0' && context.current_version[0] != '\0' &&
        is_downgrade(manifest.version.data(), context.current_version.data())) {
        return OtaManifestValidationStatus::kDowngradeRejected;
    }

    if (has_any_signature_material(manifest)) {
        if (!has_complete_signature_material(manifest)) {
            return OtaManifestValidationStatus::kMissingSignature;
        }

        char payload[kOtaManifestSigningPayloadMaxLen]{};
        if (!build_ota_manifest_signing_payload(manifest, payload, sizeof(payload)) ||
            !hal_ota_verify_manifest_signature(
                payload,
                std::strlen(payload),
                manifest.signature_algo.data(),
                manifest.signature_key_id.data(),
                manifest.signature.data())) {
            return OtaManifestValidationStatus::kInvalidSignature;
        }
    }

    return OtaManifestValidationStatus::kOk;
}

}  // namespace service
