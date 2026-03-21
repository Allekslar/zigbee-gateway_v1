/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstddef>
#include <cstdint>

#include "tuya_payload_view.hpp"

namespace service {

inline constexpr std::size_t kTuyaDpMaxPerFrame = 8U;
inline constexpr std::size_t kTuyaDpValueMaxLen = 32U;
inline constexpr std::size_t kTuyaPayloadMaxLen = 128U;

enum class TuyaDpType : uint8_t {
    kRaw = 0x00,
    kBool = 0x01,
    kValue = 0x02,
    kString = 0x03,
    kEnum = 0x04,
    kBitmap = 0x05,
    kUnknown = 0xFF,
};

struct TuyaDpItem {
    uint8_t dp_id{0};
    TuyaDpType dp_type{TuyaDpType::kUnknown};
    uint16_t value_len{0};
    uint8_t value[kTuyaDpValueMaxLen]{};

    bool as_bool() const noexcept;
    uint32_t as_u32() const noexcept;
    uint8_t as_enum() const noexcept;
    int32_t as_i32() const noexcept;
};

enum class TuyaDpParseStatus : uint8_t {
    kOk = 0,
    kPayloadTooShort,
    kPayloadTooLong,
    kDpValueTooLong,
    kDpOverflow,
    kMalformed,
};

struct TuyaDpParseResult {
    TuyaDpParseStatus status{TuyaDpParseStatus::kMalformed};
    uint16_t short_addr{0xFFFFU};
    uint8_t endpoint{0};
    uint8_t dp_count{0};
    TuyaDpItem items[kTuyaDpMaxPerFrame]{};

    const TuyaDpItem* find_dp(uint8_t dp_id) const noexcept;
};

class TuyaDpParser {
public:
    TuyaDpParseResult parse(const TuyaPayloadView& view) const noexcept;
};

}  // namespace service
