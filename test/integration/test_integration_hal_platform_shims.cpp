/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstdint>

#include "hal_matter.h"
#include "hal_mdns.h"
#include "hal_rcp.h"
#include "hal_spiffs.h"

int main() {
    assert(hal_mdns_start(nullptr) == -1);
    assert(hal_mdns_start("") == -1);
    assert(hal_mdns_start("zigbee-gateway") == 0);

    assert(hal_spiffs_mount() == 0);
    assert(hal_spiffs_mount() == 0);

    assert(hal_matter_init() == 0);
    assert(hal_matter_publish_state(1U, true) == 0);
    assert(hal_matter_publish_state(2U, false) == 0);
    assert(hal_matter_publish_attribute_update(nullptr) == -1);

    hal_matter_attribute_update_t update{};
    update.endpoint_id = 11U;
    update.attr_type = HAL_MATTER_ATTR_OCCUPANCY;
    update.bool_value = true;
    update.int_value = 0;
    assert(hal_matter_publish_attribute_update(&update) == 0);

    update.endpoint_id = 10U;
    update.attr_type = HAL_MATTER_ATTR_TEMPERATURE_CENTI_C;
    update.int_value = 2150;
    assert(hal_matter_publish_attribute_update(&update) == 0);

    const uint8_t sample_block[4] = {1U, 2U, 3U, 4U};
    assert(hal_rcp_update_begin() == 0);
    assert(hal_rcp_update_write(sample_block, sizeof(sample_block)) == 0);
    assert(hal_rcp_update_end() == 0);

    return 0;
}
