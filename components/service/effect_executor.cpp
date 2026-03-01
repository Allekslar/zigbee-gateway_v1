/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "effect_executor.hpp"

#include "hal_led.h"
#include "hal_nvs.h"
#include "hal_wifi.h"
#include "hal_zigbee.h"

namespace service {

bool EffectExecutor::execute(const core::CoreEffect& effect) noexcept {
    switch (effect.type) {
        case core::CoreEffectType::kNone:
            return true;
        case core::CoreEffectType::kPersistState:
            return hal_nvs_set_u32("core_rev", effect.arg_u32) == HAL_NVS_STATUS_OK;
        case core::CoreEffectType::kPublishTelemetry:
            return true;
        case core::CoreEffectType::kSetLed:
            return hal_led_set(effect.arg_bool) == HAL_LED_STATUS_OK;
        case core::CoreEffectType::kSendZigbeeOnOff:
            return hal_zigbee_send_on_off(effect.correlation_id, effect.device_short_addr, effect.arg_bool) ==
                   HAL_ZIGBEE_STATUS_OK;
        case core::CoreEffectType::kRefreshNetwork:
            return hal_wifi_refresh() == HAL_WIFI_STATUS_OK;
        case core::CoreEffectType::kEmitCommandResult:
            return true;
        default:
            return false;
    }
}

}  // namespace service
