/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "hal_security_state.h"

#ifdef ESP_PLATFORM
#include "esp_flash_encrypt.h"
#endif

// Test-only override hooks (declared for external callers in
// hal_security_state_test.h, gated by SERVICE_RUNTIME_TEST_HOOKS there).
// Defined unconditionally here, matching hal_identity.c's pattern, so this
// translation unit does not need to know about the test-hooks macro; on
// ESP_PLATFORM both setters are no-ops so no build profile can be used to
// spoof this security-relevant state.
void hal_security_state_set_mock_flash_encryption_enabled(bool enabled);
void hal_security_state_reset_mock_flash_encryption_enabled(void);

#ifndef ESP_PLATFORM
// Safe fail-closed default: "not verified" until a test explicitly
// overrides it.
static bool s_mock_flash_encryption_enabled = false;
#endif

bool hal_security_state_flash_encryption_enabled(void) {
#ifdef ESP_PLATFORM
    return esp_flash_encryption_enabled();
#else
    return s_mock_flash_encryption_enabled;
#endif
}

void hal_security_state_set_mock_flash_encryption_enabled(bool enabled) {
#ifndef ESP_PLATFORM
    s_mock_flash_encryption_enabled = enabled;
#else
    (void)enabled;
#endif
}

void hal_security_state_reset_mock_flash_encryption_enabled(void) {
#ifndef ESP_PLATFORM
    s_mock_flash_encryption_enabled = false;
#endif
}
