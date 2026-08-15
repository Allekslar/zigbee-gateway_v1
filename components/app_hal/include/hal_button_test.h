/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#if !defined(SERVICE_RUNTIME_TEST_HOOKS)
#error "hal_button_test.h is test-only and requires SERVICE_RUNTIME_TEST_HOOKS"
#endif

#include "hal_button.h"

#ifdef __cplusplus
extern "C" {
#endif

// Overrides the value hal_button_is_pressed() returns on host/test
// builds. No-op on ESP_PLATFORM: a real physical-presence event always
// comes from the real GPIO there, never from test-injected state (the
// same spoofing-prevention contract hal_identity_test.h/
// hal_security_state_test.h already establish for their own
// security-relevant HAL state).
void hal_button_set_mock_pressed(int pressed);

#ifdef __cplusplus
}
#endif
