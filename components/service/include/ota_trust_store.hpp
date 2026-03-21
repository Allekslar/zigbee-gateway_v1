/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

namespace service {

const char* find_ota_manifest_public_key_pem(const char* signature_key_id) noexcept;

}  // namespace service
