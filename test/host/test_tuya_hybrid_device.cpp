/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "tuya_translator.hpp"

/*
 * Hybrid-device test: proves that standard ZCL and Tuya 0xEF00 paths
 * coexist correctly for the same device.  A Tuya switch (TS0001) may
 * report on/off via 0xEF00 DP1, but its non-Tuya ZCL reports (e.g.
 * basic cluster, power config) must pass through untouched.
 */

namespace {

service::TuyaFingerprint make_switch_fp() {
    service::TuyaFingerprint fp{};
    fp.manufacturer = "_TZ3000_hybrid";
    fp.model = "TS0001";
    fp.endpoint = 1;
    return fp;
}

service::TuyaPayloadView make_view(
    uint16_t short_addr,
    uint8_t endpoint,
    uint16_t cluster_id,
    const uint8_t* data,
    uint8_t data_len) {
    service::TuyaPayloadView view{};
    view.short_addr = short_addr;
    view.endpoint = endpoint;
    view.cluster_id = cluster_id;
    view.data = data;
    view.data_len = data_len;
    return view;
}

}  // namespace

int main() {
    service::TuyaTranslator translator{};
    const auto fp = make_switch_fp();
    const uint16_t addr = 0x1234U;

    /* 1. Standard ZCL report (On/Off cluster 0x0006) → NOT routed through Tuya */
    {
        const uint8_t zcl_data[] = {0x01};
        auto view = make_view(addr, 1, 0x0006U, zcl_data, sizeof(zcl_data));
        auto result = translator.translate(view, fp);
        assert(!result.routed);
    }

    /* 2. Standard ZCL report (Basic cluster 0x0000) → NOT routed through Tuya */
    {
        const uint8_t zcl_data[] = {0x00, 0x01};
        auto view = make_view(addr, 1, 0x0000U, zcl_data, sizeof(zcl_data));
        auto result = translator.translate(view, fp);
        assert(!result.routed);
    }

    /* 3. Tuya 0xEF00 report: DP1 = power ON → routed and translated */
    {
        const uint8_t tuya_payload[] = {
            0x00, 0x01,                     /* status, seq */
            0x01, 0x01, 0x00, 0x01, 0x01,   /* dp1: bool=true (power on) */
        };
        auto view = make_view(addr, 1, service::kTuyaPrivateClusterId,
                              tuya_payload, sizeof(tuya_payload));
        auto result = translator.translate(view, fp);
        assert(result.routed);
        assert(result.parsed);
        assert(result.plugin_found);
        assert(result.plugin_result.handled);
        assert(result.plugin_result.output_count == 1);
        assert(result.plugin_result.outputs[0].kind == service::TuyaNormalizedKind::kPowerOn);
        assert(result.plugin_result.outputs[0].value == 1);
    }

    /* 4. Tuya 0xEF00 report: DP1 = power OFF → also translated */
    {
        const uint8_t tuya_payload[] = {
            0x00, 0x02,
            0x01, 0x01, 0x00, 0x01, 0x00,   /* dp1: bool=false (power off) */
        };
        auto view = make_view(addr, 1, service::kTuyaPrivateClusterId,
                              tuya_payload, sizeof(tuya_payload));
        auto result = translator.translate(view, fp);
        assert(result.routed);
        assert(result.parsed);
        assert(result.plugin_found);
        assert(result.plugin_result.handled);
        assert(result.plugin_result.outputs[0].kind == service::TuyaNormalizedKind::kPowerOn);
        assert(result.plugin_result.outputs[0].value == 0);
    }

    /* 5. Command encode: standard ZCL command kind has no Tuya encode */
    {
        service::TuyaCommandRequest request{};
        request.kind = service::TuyaNormalizedKind::kContactOpen;
        request.value = 1;
        auto encode = translator.encode_command(fp, request);
        assert(!encode.dp_command.supported);
    }

    /* 6. Command encode: kPowerOn → Tuya DP1 encode succeeds */
    {
        service::TuyaCommandRequest request{};
        request.kind = service::TuyaNormalizedKind::kPowerOn;
        request.value = 1;
        auto encode = translator.encode_command(fp, request);
        assert(encode.plugin_found);
        assert(encode.dp_command.supported);
        assert(encode.dp_command.dp_id == 1);
        assert(encode.dp_command.dp_type == service::TuyaDpType::kBool);
        assert(encode.dp_command.value[0] == 1);
    }

    /* 7. Interleaved: Tuya report then standard ZCL, then Tuya again.
     * Proves that paths do not interfere with each other. */
    {
        const uint8_t tuya_on[] = {
            0x00, 0x03,
            0x01, 0x01, 0x00, 0x01, 0x01,
        };
        auto tuya_view = make_view(addr, 1, service::kTuyaPrivateClusterId,
                                   tuya_on, sizeof(tuya_on));
        auto r1 = translator.translate(tuya_view, fp);
        assert(r1.routed && r1.plugin_result.handled);

        const uint8_t zcl_data[] = {0x00};
        auto zcl_view = make_view(addr, 1, 0x0402U, zcl_data, sizeof(zcl_data));
        auto r2 = translator.translate(zcl_view, fp);
        assert(!r2.routed);

        const uint8_t tuya_off[] = {
            0x00, 0x04,
            0x01, 0x01, 0x00, 0x01, 0x00,
        };
        auto tuya_view2 = make_view(addr, 1, service::kTuyaPrivateClusterId,
                                    tuya_off, sizeof(tuya_off));
        auto r3 = translator.translate(tuya_view2, fp);
        assert(r3.routed && r3.plugin_result.handled);
        assert(r3.plugin_result.outputs[0].value == 0);
    }

    return 0;
}
