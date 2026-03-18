/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

namespace service {

enum class OtaBootConfirmResult : uint8_t {
    kNotRequired = 0,
    kConfirmed = 1,
    kFailed = 2,
};

OtaBootConfirmResult confirm_pending_ota_image() noexcept;

}  // namespace service
