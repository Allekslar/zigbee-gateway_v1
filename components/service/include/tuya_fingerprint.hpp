/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstddef>
#include <cstdint>

#include "device_descriptor_store.hpp"

namespace service {

struct TuyaFingerprint {
    static constexpr std::size_t kManufacturerMaxLen = kDeviceDescriptorManufacturerMaxLen;
    static constexpr std::size_t kModelMaxLen = kDeviceDescriptorModelMaxLen;

    const char* manufacturer{nullptr};
    const char* model{nullptr};
    uint8_t endpoint{0};
};

enum class TuyaFingerprintMatchResult : uint8_t {
    kNoMatch = 0,
    kMatched = 1,
};

class TuyaFingerprintResolver {
public:
    TuyaFingerprintMatchResult resolve(const TuyaFingerprint& fingerprint) const noexcept;
    bool is_tuya_manufacturer(const char* manufacturer) const noexcept;
};

}  // namespace service
