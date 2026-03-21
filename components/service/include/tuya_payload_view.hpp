/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstddef>
#include <cstdint>

namespace service {

inline constexpr uint16_t kTuyaPrivateClusterId = 0xEF00U;
inline constexpr uint16_t kTuyaManufacturerCode = 0x1002U;

struct TuyaPayloadView {
    uint16_t short_addr{0xFFFFU};
    uint8_t endpoint{0};
    uint16_t cluster_id{0};
    const uint8_t* data{nullptr};
    uint8_t data_len{0};

    bool is_tuya_cluster() const noexcept {
        return cluster_id == kTuyaPrivateClusterId;
    }

    bool is_valid() const noexcept {
        return data != nullptr && data_len > 0U && short_addr != 0xFFFFU;
    }
};

}  // namespace service
