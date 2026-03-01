/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstddef>

#include "hal_wifi.h"

int main() {
    assert(hal_wifi_init() == HAL_WIFI_STATUS_OK);

    hal_wifi_scan_record_t records[2]{};
    std::size_t found_count = 0;
    assert(hal_wifi_scan(records, 2U, &found_count) == HAL_WIFI_STATUS_OK);
    assert(found_count <= 2U);

    return 0;
}
