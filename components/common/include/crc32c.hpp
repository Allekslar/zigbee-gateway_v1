/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstddef>
#include <cstdint>

namespace common {

// CRC-32C (Castagnoli, polynomial 0x1EDC6F41), bit-reflected, initial/final
// XOR 0xFFFFFFFF -- the same construction used by iSCSI/ext4/NVMe. Used to
// protect explicit persisted-record payloads in addition to the NVS layer's
// own transport integrity (plan Section 7.3).
uint32_t crc32c(const void* data, std::size_t len) noexcept;

}  // namespace common
