/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include "hal_rcp.h"
#include "service_runtime_api.hpp"

namespace service {

bool build_rcp_transport_request(const RcpUpdateRequest& request, hal_rcp_https_request_t* out) noexcept;

}  // namespace service
