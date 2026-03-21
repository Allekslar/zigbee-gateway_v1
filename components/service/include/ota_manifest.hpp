/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace service {

inline constexpr std::size_t kOtaManifestUrlMaxLen = 192U;
inline constexpr std::size_t kOtaManifestVersionMaxLen = 32U;
inline constexpr std::size_t kOtaManifestProjectMaxLen = 32U;
inline constexpr std::size_t kOtaManifestBoardMaxLen = 32U;
inline constexpr std::size_t kOtaManifestChipTargetMaxLen = 24U;
inline constexpr std::size_t kOtaManifestSha256HexLen = 64U;
inline constexpr std::size_t kOtaManifestSha256MaxLen = kOtaManifestSha256HexLen + 1U;
inline constexpr std::size_t kOtaManifestSignatureAlgoMaxLen = 32U;
inline constexpr std::size_t kOtaManifestSignatureKeyIdMaxLen = 32U;
inline constexpr std::size_t kOtaManifestSignatureHexLen = 192U;
inline constexpr std::size_t kOtaManifestSignatureMaxLen = kOtaManifestSignatureHexLen + 1U;
inline constexpr std::size_t kOtaManifestSigningPayloadMaxLen = 512U;

struct OtaManifest {
    std::array<char, kOtaManifestUrlMaxLen> url{};
    std::array<char, kOtaManifestVersionMaxLen> version{};
    std::array<char, kOtaManifestProjectMaxLen> project{};
    std::array<char, kOtaManifestBoardMaxLen> board{};
    std::array<char, kOtaManifestChipTargetMaxLen> chip_target{};
    std::array<char, kOtaManifestSha256MaxLen> sha256{};
    std::array<char, kOtaManifestSignatureAlgoMaxLen> signature_algo{};
    std::array<char, kOtaManifestSignatureKeyIdMaxLen> signature_key_id{};
    std::array<char, kOtaManifestSignatureMaxLen> signature{};
    uint32_t min_schema{0};
    bool allow_downgrade{false};
};

struct OtaManifestContext {
    std::array<char, kOtaManifestVersionMaxLen> current_version{};
    std::array<char, kOtaManifestProjectMaxLen> current_project{};
    std::array<char, kOtaManifestBoardMaxLen> current_board{};
    std::array<char, kOtaManifestChipTargetMaxLen> current_chip_target{};
    uint32_t current_schema{0};
};

enum class OtaManifestValidationStatus : uint8_t {
    kOk = 0,
    kInvalidUrl = 1,
    kInvalidSha256 = 2,
    kProjectMismatch = 3,
    kBoardMismatch = 4,
    kChipTargetMismatch = 5,
    kSchemaTooNew = 6,
    kDowngradeRejected = 7,
    kMissingSignature = 8,
    kInvalidSignature = 9,
};

void apply_ota_manifest_defaults(const OtaManifestContext& context, OtaManifest* manifest) noexcept;
bool build_ota_manifest_signing_payload(
    const OtaManifest& manifest,
    char* out,
    std::size_t out_capacity) noexcept;
bool verify_ota_manifest_signature(const OtaManifest& manifest) noexcept;
OtaManifestValidationStatus validate_ota_manifest(
    const OtaManifest& manifest,
    const OtaManifestContext& context) noexcept;

}  // namespace service
