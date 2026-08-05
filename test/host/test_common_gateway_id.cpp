/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <array>
#include <cassert>
#include <cstring>

#include "gateway_id.hpp"

int main() {
    // Default-constructed GatewayId is all-zero, which is explicitly invalid
    // (an unprogrammed eFuse base MAC, not a real factory identity).
    common::GatewayId default_id{};
    assert(!default_id.valid());

    // Format: exactly 12 lowercase hex characters, no separators.
    const std::array<uint8_t, common::GatewayId::kByteLength> bytes = {0x00, 0x12, 0x4b, 0xab, 0xcd, 0xef};
    common::GatewayId id(bytes);
    assert(id.valid());
    char formatted[common::GatewayId::kHexLength] = {};
    assert(id.format(formatted, sizeof(formatted)));
    assert(std::memcmp(formatted, "00124babcdef", common::GatewayId::kHexLength) == 0);

    // Byte order is preserved as constructed (big-endian factory MAC order).
    assert(id.bytes()[0] == 0x00U);
    assert(id.bytes()[1] == 0x12U);
    assert(id.bytes()[5] == 0xefU);

    // Equality.
    common::GatewayId same(bytes);
    assert(id == same);
    assert(!(id != same));

    const std::array<uint8_t, common::GatewayId::kByteLength> other_bytes = {0x00, 0x12, 0x4b, 0xab, 0xcd, 0xf0};
    common::GatewayId other(other_bytes);
    assert(id != other);
    assert(!(id == other));

    // format() rejects an undersized buffer.
    char too_small[common::GatewayId::kHexLength - 1U] = {};
    assert(!id.format(too_small, sizeof(too_small)));
    assert(!id.format(nullptr, common::GatewayId::kHexLength));

    // No heap allocation / trivial value semantics: copy is a plain memberwise copy.
    common::GatewayId copy = id;
    assert(copy == id);

    return 0;
}
