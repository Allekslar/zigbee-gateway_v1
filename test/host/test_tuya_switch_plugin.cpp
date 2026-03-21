/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "tuya_switch_plugin.hpp"
#include "tuya_dp_parser.hpp"

int main() {
    service::TuyaSwitchPlugin plugin{};

    /* Matches TS0001 with _TZ prefix */
    {
        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_abc";
        fp.model = "TS0001";
        fp.endpoint = 1;
        assert(plugin.matches(fp));
    }

    /* Matches TS0011 with _TZ prefix */
    {
        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_abc";
        fp.model = "TS0011";
        fp.endpoint = 1;
        assert(plugin.matches(fp));
    }

    /* Does not match TS0203 (contact sensor) */
    {
        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_abc";
        fp.model = "TS0203";
        fp.endpoint = 1;
        assert(!plugin.matches(fp));
    }

    /* Does not match non-Tuya manufacturer */
    {
        service::TuyaFingerprint fp{};
        fp.manufacturer = "IKEA";
        fp.model = "TS0001";
        fp.endpoint = 1;
        assert(!plugin.matches(fp));
    }

    /* Translate inbound: power on (DP1=true) */
    {
        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_test";
        fp.model = "TS0001";

        service::TuyaDpParseResult dp{};
        dp.status = service::TuyaDpParseStatus::kOk;
        dp.short_addr = 0x1234U;
        dp.endpoint = 1;
        dp.dp_count = 1;

        dp.items[0].dp_id = 1;
        dp.items[0].dp_type = service::TuyaDpType::kBool;
        dp.items[0].value_len = 1;
        dp.items[0].value[0] = 1;

        auto result = plugin.translate(fp, dp);
        assert(result.handled);
        assert(result.output_count == 1);
        assert(result.outputs[0].kind == service::TuyaNormalizedKind::kPowerOn);
        assert(result.outputs[0].value == 1);
    }

    /* Translate inbound: power off (DP1=false) */
    {
        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_test";
        fp.model = "TS0001";

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
        assert(result.outputs[0].kind == service::TuyaNormalizedKind::kPowerOn);
        assert(result.outputs[0].value == 0);
    }

    /* Unknown DP: not handled */
    {
        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_test";
        fp.model = "TS0001";

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

    /* Encode command: power on */
    {
        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_test";
        fp.model = "TS0001";

        service::TuyaCommandRequest request{};
        request.kind = service::TuyaNormalizedKind::kPowerOn;
        request.value = 1;

        auto cmd = plugin.encode_command(fp, request);
        assert(cmd.supported);
        assert(cmd.dp_id == 1);
        assert(cmd.dp_type == service::TuyaDpType::kBool);
        assert(cmd.value_len == 1);
        assert(cmd.value[0] == 1);
        assert(cmd.endpoint == 1);
    }

    /* Encode command: power off */
    {
        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_test";
        fp.model = "TS0001";

        service::TuyaCommandRequest request{};
        request.kind = service::TuyaNormalizedKind::kPowerOn;
        request.value = 0;

        auto cmd = plugin.encode_command(fp, request);
        assert(cmd.supported);
        assert(cmd.dp_id == 1);
        assert(cmd.dp_type == service::TuyaDpType::kBool);
        assert(cmd.value_len == 1);
        assert(cmd.value[0] == 0);
    }

    /* Encode unsupported command kind */
    {
        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_test";
        fp.model = "TS0001";

        service::TuyaCommandRequest request{};
        request.kind = service::TuyaNormalizedKind::kContactOpen;
        request.value = 1;

        auto cmd = plugin.encode_command(fp, request);
        assert(!cmd.supported);
    }

    return 0;
}
