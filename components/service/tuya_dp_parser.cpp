/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "tuya_dp_parser.hpp"

#include <cstring>

namespace service {

bool TuyaDpItem::as_bool() const noexcept {
    if (value_len == 0U) {
        return false;
    }
    return value[0] != 0U;
}

uint32_t TuyaDpItem::as_u32() const noexcept {
    /* Tuya values are big-endian. */
    uint32_t result = 0U;
    const std::size_t len = (value_len > 4U) ? 4U : static_cast<std::size_t>(value_len);
    for (std::size_t i = 0; i < len; ++i) {
        result = (result << 8U) | value[i];
    }
    return result;
}

int32_t TuyaDpItem::as_i32() const noexcept {
    return static_cast<int32_t>(as_u32());
}

uint8_t TuyaDpItem::as_enum() const noexcept {
    if (value_len == 0U) {
        return 0U;
    }
    return value[0];
}

const TuyaDpItem* TuyaDpParseResult::find_dp(uint8_t dp_id) const noexcept {
    for (uint8_t i = 0; i < dp_count; ++i) {
        if (items[i].dp_id == dp_id) {
            return &items[i];
        }
    }
    return nullptr;
}

TuyaDpParseResult TuyaDpParser::parse(const TuyaPayloadView& view) const noexcept {
    TuyaDpParseResult result{};
    result.short_addr = view.short_addr;
    result.endpoint = view.endpoint;

    if (!view.is_valid() || !view.is_tuya_cluster()) {
        result.status = TuyaDpParseStatus::kMalformed;
        return result;
    }

    if (view.data_len < 2U) {
        result.status = TuyaDpParseStatus::kPayloadTooShort;
        return result;
    }

    if (view.data_len > kTuyaPayloadMaxLen) {
        result.status = TuyaDpParseStatus::kPayloadTooLong;
        return result;
    }

    /*
     * Tuya 0xEF00 cluster-specific command payload (report, command 0x01/0x02):
     *   byte 0: status (typically 0)
     *   byte 1: transaction sequence number
     *   bytes 2..N: DP records
     *
     * Each DP record:
     *   dp_id:    1 byte
     *   dp_type:  1 byte
     *   dp_len:   2 bytes (big-endian)
     *   dp_value: dp_len bytes
     */
    std::size_t offset = 2U;
    const std::size_t total_len = view.data_len;

    while (offset < total_len) {
        if (result.dp_count >= kTuyaDpMaxPerFrame) {
            result.status = TuyaDpParseStatus::kDpOverflow;
            return result;
        }

        /* Need at least 4 bytes for dp_id + dp_type + dp_len. */
        if (offset + 4U > total_len) {
            result.status = TuyaDpParseStatus::kMalformed;
            return result;
        }

        TuyaDpItem& item = result.items[result.dp_count];
        item.dp_id = view.data[offset];
        const uint8_t raw_type = view.data[offset + 1U];
        item.dp_type = (raw_type <= static_cast<uint8_t>(TuyaDpType::kBitmap))
                           ? static_cast<TuyaDpType>(raw_type)
                           : TuyaDpType::kUnknown;

        const uint16_t dp_len = static_cast<uint16_t>(
            (static_cast<uint16_t>(view.data[offset + 2U]) << 8U) |
            static_cast<uint16_t>(view.data[offset + 3U]));
        offset += 4U;

        if (dp_len > kTuyaDpValueMaxLen) {
            result.status = TuyaDpParseStatus::kDpValueTooLong;
            return result;
        }

        if (offset + dp_len > total_len) {
            result.status = TuyaDpParseStatus::kMalformed;
            return result;
        }

        item.value_len = dp_len;
        if (dp_len > 0U) {
            std::memcpy(item.value, &view.data[offset], dp_len);
        }
        offset += dp_len;

        ++result.dp_count;
    }

    result.status = TuyaDpParseStatus::kOk;
    return result;
}

}  // namespace service
