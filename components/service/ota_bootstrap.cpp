/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "ota_bootstrap.hpp"

#include "hal_ota.h"

namespace service {

OtaBootConfirmResult confirm_pending_ota_image() noexcept {
    if (!hal_ota_running_partition_pending_verify()) {
        return OtaBootConfirmResult::kNotRequired;
    }

    if (hal_ota_mark_running_partition_valid() != 0) {
        return OtaBootConfirmResult::kFailed;
    }

    return OtaBootConfirmResult::kConfirmed;
}

}  // namespace service
