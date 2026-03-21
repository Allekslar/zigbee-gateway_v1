/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "tuya_dp_parser.hpp"

namespace {

service::TuyaPayloadView make_view(
    uint16_t short_addr,
    const uint8_t* data,
    uint8_t data_len) {
    service::TuyaPayloadView view{};
    view.short_addr = short_addr;
    view.endpoint = 1;
    view.cluster_id = service::kTuyaPrivateClusterId;
    view.data = data;
    view.data_len = data_len;
    return view;
}

}  // namespace

int main() {
    service::TuyaDpParser parser{};

    /* Valid single bool DP: status(1) + seq(1) + dp_id(1) + dp_type(1) + len(2) + value(1) */
    {
        const uint8_t payload[] = {
            0x00, 0x01,       /* status=0, seq=1 */
            0x01,             /* dp_id=1 */
            0x01,             /* dp_type=bool */
            0x00, 0x01,       /* dp_len=1 (big-endian) */
            0x01,             /* value=true */
        };
        auto view = make_view(0x1234U, payload, sizeof(payload));
        auto result = parser.parse(view);
        assert(result.status == service::TuyaDpParseStatus::kOk);
        assert(result.short_addr == 0x1234U);
        assert(result.dp_count == 1);
        assert(result.items[0].dp_id == 1);
        assert(result.items[0].dp_type == service::TuyaDpType::kBool);
        assert(result.items[0].value_len == 1);
        assert(result.items[0].as_bool() == true);
    }

    /* Valid single value DP (u32 big-endian) */
    {
        const uint8_t payload[] = {
            0x00, 0x02,       /* status=0, seq=2 */
            0x03,             /* dp_id=3 */
            0x02,             /* dp_type=value */
            0x00, 0x04,       /* dp_len=4 */
            0x00, 0x00, 0x00, 0x42,  /* value=66 */
        };
        auto view = make_view(0x5678U, payload, sizeof(payload));
        auto result = parser.parse(view);
        assert(result.status == service::TuyaDpParseStatus::kOk);
        assert(result.dp_count == 1);
        assert(result.items[0].dp_id == 3);
        assert(result.items[0].dp_type == service::TuyaDpType::kValue);
        assert(result.items[0].as_u32() == 66U);
    }

    /* Multiple DPs in one frame */
    {
        const uint8_t payload[] = {
            0x00, 0x03,       /* status=0, seq=3 */
            0x01, 0x01, 0x00, 0x01, 0x01,  /* dp1: bool=true */
            0x03, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x55,  /* dp3: value=85 */
        };
        auto view = make_view(0xAAAAU, payload, sizeof(payload));
        auto result = parser.parse(view);
        assert(result.status == service::TuyaDpParseStatus::kOk);
        assert(result.dp_count == 2);
        assert(result.items[0].dp_id == 1);
        assert(result.items[0].as_bool() == true);
        assert(result.items[1].dp_id == 3);
        assert(result.items[1].as_u32() == 85U);
    }

    /* find_dp helper */
    {
        const uint8_t payload[] = {
            0x00, 0x04,
            0x01, 0x01, 0x00, 0x01, 0x00,  /* dp1: bool=false */
            0x03, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x32,  /* dp3: value=50 */
        };
        auto view = make_view(0xBBBBU, payload, sizeof(payload));
        auto result = parser.parse(view);
        assert(result.status == service::TuyaDpParseStatus::kOk);

        const service::TuyaDpItem* dp1 = result.find_dp(1);
        assert(dp1 != nullptr);
        assert(dp1->as_bool() == false);

        const service::TuyaDpItem* dp3 = result.find_dp(3);
        assert(dp3 != nullptr);
        assert(dp3->as_u32() == 50U);

        assert(result.find_dp(99) == nullptr);
    }

    /* Enum DP type */
    {
        const uint8_t payload[] = {
            0x00, 0x05,
            0x04, 0x04, 0x00, 0x01, 0x02,  /* dp4: enum=2 */
        };
        auto view = make_view(0xCCCCU, payload, sizeof(payload));
        auto result = parser.parse(view);
        assert(result.status == service::TuyaDpParseStatus::kOk);
        assert(result.items[0].dp_type == service::TuyaDpType::kEnum);
        assert(result.items[0].as_enum() == 2U);
    }

    /* Reject too-short payload */
    {
        const uint8_t payload[] = {0x00};
        auto view = make_view(0x1111U, payload, 1);
        auto result = parser.parse(view);
        assert(result.status == service::TuyaDpParseStatus::kPayloadTooShort);
    }

    /* Reject non-tuya cluster */
    {
        const uint8_t payload[] = {0x00, 0x01, 0x01, 0x01, 0x00, 0x01, 0x01};
        service::TuyaPayloadView view{};
        view.short_addr = 0x1234U;
        view.endpoint = 1;
        view.cluster_id = 0x0006U;
        view.data = payload;
        view.data_len = sizeof(payload);
        auto result = parser.parse(view);
        assert(result.status == service::TuyaDpParseStatus::kMalformed);
    }

    /* Reject truncated DP (dp_len exceeds remaining data) */
    {
        const uint8_t payload[] = {
            0x00, 0x01,
            0x01, 0x01, 0x00, 0x05, 0x01,  /* dp_len=5 but only 1 byte left */
        };
        auto view = make_view(0x2222U, payload, sizeof(payload));
        auto result = parser.parse(view);
        assert(result.status == service::TuyaDpParseStatus::kMalformed);
    }

    /* as_i32 for signed values via two's complement */
    {
        const uint8_t payload[] = {
            0x00, 0x06,
            0x0E, 0x02, 0x00, 0x04,
            0xFF, 0xFF, 0xFF, 0x9C,  /* -100 in big-endian two's complement */
        };
        auto view = make_view(0x3333U, payload, sizeof(payload));
        auto result = parser.parse(view);
        assert(result.status == service::TuyaDpParseStatus::kOk);
        assert(result.items[0].as_i32() == -100);
    }

    return 0;
}
