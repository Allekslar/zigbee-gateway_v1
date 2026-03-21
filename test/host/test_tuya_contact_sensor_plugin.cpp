/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "tuya_contact_sensor_plugin.hpp"
#include "tuya_dp_parser.hpp"

int main() {
    service::TuyaContactSensorPlugin plugin{};

    /* Matches TS0203 with _TZ prefix */
    {
        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_abc";
        fp.model = "TS0203";
        fp.endpoint = 1;
        assert(plugin.matches(fp));
    }

    /* Does not match non-TS0203 model */
    {
        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_abc";
        fp.model = "TS0001";
        fp.endpoint = 1;
        assert(!plugin.matches(fp));
    }

    /* Does not match non-Tuya manufacturer */
    {
        service::TuyaFingerprint fp{};
        fp.manufacturer = "IKEA";
        fp.model = "TS0203";
        fp.endpoint = 1;
        assert(!plugin.matches(fp));
    }

    /* Translate: contact open + battery */
    {
        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_test";
        fp.model = "TS0203";

        service::TuyaDpParseResult dp{};
        dp.status = service::TuyaDpParseStatus::kOk;
        dp.short_addr = 0x1234U;
        dp.endpoint = 1;
        dp.dp_count = 2;

        /* DP 1: contact = true (open) */
        dp.items[0].dp_id = 1;
        dp.items[0].dp_type = service::TuyaDpType::kBool;
        dp.items[0].value_len = 1;
        dp.items[0].value[0] = 1;

        /* DP 3: battery = 85% */
        dp.items[1].dp_id = 3;
        dp.items[1].dp_type = service::TuyaDpType::kValue;
        dp.items[1].value_len = 4;
        dp.items[1].value[0] = 0;
        dp.items[1].value[1] = 0;
        dp.items[1].value[2] = 0;
        dp.items[1].value[3] = 85;

        auto result = plugin.translate(fp, dp);
        assert(result.handled);
        assert(result.output_count == 2);
        assert(result.outputs[0].kind == service::TuyaNormalizedKind::kContactOpen);
        assert(result.outputs[0].value == 1);
        assert(result.outputs[1].kind == service::TuyaNormalizedKind::kBatteryPercent);
        assert(result.outputs[1].value == 85);
    }

    /* Translate: contact closed, no battery */
    {
        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_test";
        fp.model = "TS0203";

        service::TuyaDpParseResult dp{};
        dp.status = service::TuyaDpParseStatus::kOk;
        dp.dp_count = 1;
        dp.items[0].dp_id = 1;
        dp.items[0].dp_type = service::TuyaDpType::kBool;
        dp.items[0].value_len = 1;
        dp.items[0].value[0] = 0;

        auto result = plugin.translate(fp, dp);
        assert(result.handled);
        assert(result.output_count == 1);
        assert(result.outputs[0].kind == service::TuyaNormalizedKind::kContactOpen);
        assert(result.outputs[0].value == 0);
    }

    /* Unknown DP: not handled */
    {
        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_test";
        fp.model = "TS0203";

        service::TuyaDpParseResult dp{};
        dp.status = service::TuyaDpParseStatus::kOk;
        dp.dp_count = 1;
        dp.items[0].dp_id = 99;
        dp.items[0].dp_type = service::TuyaDpType::kBool;
        dp.items[0].value_len = 1;
        dp.items[0].value[0] = 1;

        auto result = plugin.translate(fp, dp);
        assert(!result.handled);
        assert(result.output_count == 0);
    }

    return 0;
}
