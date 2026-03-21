/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "tuya_translator.hpp"

int main() {
    service::TuyaTranslator translator{};

    /* Encode power-on command for known switch model */
    {
        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_test";
        fp.model = "TS0001";
        fp.endpoint = 1;

        service::TuyaCommandRequest request{};
        request.kind = service::TuyaNormalizedKind::kPowerOn;
        request.value = 1;

        auto result = translator.encode_command(fp, request);
        assert(result.plugin_found);
        assert(result.dp_command.supported);
        assert(result.dp_command.dp_id == 1);
        assert(result.dp_command.dp_type == service::TuyaDpType::kBool);
        assert(result.dp_command.value_len == 1);
        assert(result.dp_command.value[0] == 1);
    }

    /* Encode power-off command for known switch model */
    {
        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_test";
        fp.model = "TS0001";
        fp.endpoint = 1;

        service::TuyaCommandRequest request{};
        request.kind = service::TuyaNormalizedKind::kPowerOn;
        request.value = 0;

        auto result = translator.encode_command(fp, request);
        assert(result.plugin_found);
        assert(result.dp_command.supported);
        assert(result.dp_command.value[0] == 0);
    }

    /* Unknown model: no plugin found */
    {
        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_unknown";
        fp.model = "TS9999";
        fp.endpoint = 1;

        service::TuyaCommandRequest request{};
        request.kind = service::TuyaNormalizedKind::kPowerOn;
        request.value = 1;

        auto result = translator.encode_command(fp, request);
        assert(!result.plugin_found);
        assert(!result.dp_command.supported);
    }

    /* Contact sensor: plugin found but command not supported */
    {
        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_test";
        fp.model = "TS0203";
        fp.endpoint = 1;

        service::TuyaCommandRequest request{};
        request.kind = service::TuyaNormalizedKind::kPowerOn;
        request.value = 1;

        auto result = translator.encode_command(fp, request);
        assert(result.plugin_found);
        assert(!result.dp_command.supported);
    }

    /* Switch: unsupported command kind (contact is not a controllable attribute) */
    {
        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_test";
        fp.model = "TS0001";
        fp.endpoint = 1;

        service::TuyaCommandRequest request{};
        request.kind = service::TuyaNormalizedKind::kContactOpen;
        request.value = 1;

        auto result = translator.encode_command(fp, request);
        assert(result.plugin_found);
        assert(!result.dp_command.supported);
    }

    /* Full round-trip: translate inbound + encode outbound for same device */
    {
        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_test";
        fp.model = "TS0001";
        fp.endpoint = 1;

        /* Inbound: power on report */
        const uint8_t payload[] = {
            0x00, 0x01,                     /* status, seq */
            0x01, 0x01, 0x00, 0x01, 0x01,   /* dp1: bool=true */
        };
        service::TuyaPayloadView view{};
        view.short_addr = 0x1234U;
        view.endpoint = 1;
        view.cluster_id = service::kTuyaPrivateClusterId;
        view.data = payload;
        view.data_len = sizeof(payload);

        auto translate_result = translator.translate(view, fp);
        assert(translate_result.routed);
        assert(translate_result.parsed);
        assert(translate_result.plugin_found);
        assert(translate_result.plugin_result.handled);
        assert(translate_result.plugin_result.outputs[0].kind == service::TuyaNormalizedKind::kPowerOn);
        assert(translate_result.plugin_result.outputs[0].value == 1);

        /* Outbound: power off command */
        service::TuyaCommandRequest request{};
        request.kind = service::TuyaNormalizedKind::kPowerOn;
        request.value = 0;

        auto encode_result = translator.encode_command(fp, request);
        assert(encode_result.plugin_found);
        assert(encode_result.dp_command.supported);
        assert(encode_result.dp_command.dp_id == 1);
        assert(encode_result.dp_command.value[0] == 0);
    }

    return 0;
}
