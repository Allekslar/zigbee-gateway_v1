/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#if !defined(SERVICE_RUNTIME_TEST_HOOKS)
#error "hal_security_state_test.h is test-only and requires SERVICE_RUNTIME_TEST_HOOKS"
#endif

#include "hal_security_state.h"

#ifdef __cplusplus
extern "C" {
#endif

// Overrides the value hal_security_state_flash_encryption_enabled()
// returns on host/test builds (e.g. to exercise both the "encryption
// verified" and "encryption not verified" branches of the plan #12 write
// gate in secure_storage_port.cpp without real hardware). No-op on
// ESP_PLATFORM: the real eFuse-backed state is always read there, never
// test-injected (matching hal_identity_test.h's own base-MAC override
// contract for the same reason -- security-relevant HAL state must not be
// spoofable on a real device).
void hal_security_state_set_mock_flash_encryption_enabled(bool enabled);

// Restores the default mock state (false -- "not verified", the safe
// fail-closed default). Host/test builds only; no-op on ESP_PLATFORM.
void hal_security_state_reset_mock_flash_encryption_enabled(void);

#ifdef __cplusplus
}
#endif
