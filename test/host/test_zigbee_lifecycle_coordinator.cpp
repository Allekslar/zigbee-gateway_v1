/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "network_policy_manager.hpp"
#include "zigbee_lifecycle_coordinator.hpp"

int main() {
    service::NetworkPolicyManager network_policy_manager{};
    service::ZigbeeLifecycleCoordinator coordinator(network_policy_manager);
    uint16_t seconds_left = 0U;

    assert(!coordinator.get_join_window_status(&seconds_left));
    assert(seconds_left == 0U);

    coordinator.set_join_window_cache(true, 33U);
    assert(coordinator.get_join_window_status(&seconds_left));
    assert(seconds_left == 33U);

    coordinator.set_join_window_cache(false, 99U);
    assert(!coordinator.get_join_window_status(&seconds_left));
    assert(seconds_left == 0U);
    return 0;
}
