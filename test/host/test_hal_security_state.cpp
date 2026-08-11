/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>

#include "hal_security_state.h"
#include "hal_security_state_test.h"

int main() {
    // The safe fail-closed default: "not verified" until a test explicitly
    // overrides it.
    hal_security_state_reset_mock_flash_encryption_enabled();
    assert(hal_security_state_flash_encryption_enabled() == false);

    hal_security_state_set_mock_flash_encryption_enabled(true);
    assert(hal_security_state_flash_encryption_enabled() == true);

    hal_security_state_set_mock_flash_encryption_enabled(false);
    assert(hal_security_state_flash_encryption_enabled() == false);

    hal_security_state_reset_mock_flash_encryption_enabled();
    assert(hal_security_state_flash_encryption_enabled() == false);

    return 0;
}
