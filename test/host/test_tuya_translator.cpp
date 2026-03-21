/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "tuya_translator.hpp"

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
    service::TuyaTranslator translator{};

    /* Full pipeline: TS0203 contact sensor with known fingerprint */
    {
        /* DP payload: contact open (dp1=true) */
        const uint8_t payload[] = {
            0x00, 0x01,                     /* status, seq */
            0x01, 0x01, 0x00, 0x01, 0x01,   /* dp1: bool=true */
        };
        auto view = make_view(0x1234U, payload, sizeof(payload));

        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_test";
        fp.model = "TS0203";
        fp.endpoint = 1;

        auto result = translator.translate(view, fp);
        assert(result.routed);
        assert(result.parsed);
        assert(result.plugin_found);
        assert(result.plugin_result.handled);
        assert(result.plugin_result.output_count == 1);
        assert(result.plugin_result.outputs[0].kind == service::TuyaNormalizedKind::kContactOpen);
        assert(result.plugin_result.outputs[0].value == 1);
    }

    /* Unknown model: routed + parsed but no plugin */
    {
        const uint8_t payload[] = {
            0x00, 0x02,
            0x01, 0x01, 0x00, 0x01, 0x00,
        };
        auto view = make_view(0x5678U, payload, sizeof(payload));

        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_unknown";
        fp.model = "TS9999";
        fp.endpoint = 1;

        auto result = translator.translate(view, fp);
        assert(result.routed);
        assert(result.parsed);
        assert(!result.plugin_found);
    }

    /* Non-Tuya cluster: not routed */
    {
        const uint8_t payload[] = {0x00, 0x01};
        service::TuyaPayloadView view{};
        view.short_addr = 0x1234U;
        view.endpoint = 1;
        view.cluster_id = 0x0006U;
        view.data = payload;
        view.data_len = sizeof(payload);

        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_test";
        fp.model = "TS0203";

        auto result = translator.translate(view, fp);
        assert(!result.routed);
    }

    /* Malformed payload: routed but not parsed */
    {
        const uint8_t payload[] = {0x00};
        auto view = make_view(0x9999U, payload, 1);

        service::TuyaFingerprint fp{};
        fp.manufacturer = "_TZ3000_test";
        fp.model = "TS0203";

        auto result = translator.translate(view, fp);
        assert(result.routed);
        assert(!result.parsed);
    }

    return 0;
}
