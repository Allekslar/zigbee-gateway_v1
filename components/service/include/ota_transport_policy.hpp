/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include "hal_ota.h"
#include "service_runtime_api.hpp"

namespace service {

bool build_ota_transport_request(const OtaStartRequest& request, hal_ota_https_request_t* out) noexcept;

}  // namespace service
