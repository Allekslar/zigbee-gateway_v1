/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#if !defined(SERVICE_RUNTIME_TEST_HOOKS)
#error "hal_identity_test.h is test-only and requires SERVICE_RUNTIME_TEST_HOOKS"
#endif

#include "hal_identity.h"

#ifdef __cplusplus
extern "C" {
#endif

// Overrides the value hal_identity_get_factory_base_mac() returns on host/
// test builds (e.g. to prove two different base MACs yield two different
// GatewayIds). `mac` must have at least HAL_IDENTITY_BASE_MAC_LEN bytes.
// No-op on ESP_PLATFORM: GatewayId always derives from the real eFuse
// value there, never from test-injected state (FD-17 identity integrity).
void hal_identity_set_mock_base_mac(const uint8_t* mac);

// Restores the default mock base MAC (host/test builds only; no-op on
// ESP_PLATFORM).
void hal_identity_reset_mock_base_mac(void);

#ifdef __cplusplus
}
#endif
