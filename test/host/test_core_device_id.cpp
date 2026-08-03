/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "device_id.hpp"

int main() {
    // Default-constructed DeviceId is all-zero, which is explicitly invalid.
    core::DeviceId default_id{};
    assert(!default_id.valid());

    // Round trip: parse -> format -> compare text.
    core::DeviceId parsed{};
    assert(core::DeviceId::parse("00124b0001abcdef", 16, &parsed));
    assert(parsed.valid());
    char formatted[core::DeviceId::kHexLength] = {};
    assert(parsed.format(formatted, sizeof(formatted)));
    assert(std::memcmp(formatted, "00124b0001abcdef", core::DeviceId::kHexLength) == 0);

    // Canonical byte order: most-significant byte first.
    assert(parsed.bytes()[0] == 0x00U);
    assert(parsed.bytes()[1] == 0x12U);
    assert(parsed.bytes()[2] == 0x4bU);
    assert(parsed.bytes()[7] == 0xefU);

    // Equality/ordering.
    core::DeviceId parsed_again{};
    assert(core::DeviceId::parse("00124b0001abcdef", 16, &parsed_again));
    assert(parsed == parsed_again);
    assert(!(parsed != parsed_again));

    core::DeviceId higher{};
    assert(core::DeviceId::parse("00124b0001abcdf0", 16, &higher));
    assert(parsed < higher);
    assert(!(higher < parsed));

    // All-zero and all-ff are reserved/invalid.
    core::DeviceId all_zero{};
    assert(core::DeviceId::parse("0000000000000000", 16, &all_zero));
    assert(!all_zero.valid());

    core::DeviceId all_ff{};
    assert(core::DeviceId::parse("ffffffffffffffff", 16, &all_ff));
    assert(!all_ff.valid());

    // Rejected inputs: wrong length, uppercase hex, non-hex characters, null.
    core::DeviceId reject{};
    assert(!core::DeviceId::parse("00124b0001abcde", 15, &reject));   // too short
    assert(!core::DeviceId::parse("00124b0001abcdefff", 18, &reject));  // too long
    assert(!core::DeviceId::parse("00124B0001ABCDEF", 16, &reject));  // uppercase rejected
    assert(!core::DeviceId::parse("00124g0001abcdef", 16, &reject));  // non-hex 'g'
    assert(!core::DeviceId::parse(nullptr, 16, &reject));

    // format() rejects an undersized buffer.
    char too_small[core::DeviceId::kHexLength - 1U] = {};
    assert(!parsed.format(too_small, sizeof(too_small)));

    // No heap allocation / trivial value semantics: copy is a plain memberwise copy.
    core::DeviceId copy = parsed;
    assert(copy == parsed);

    return 0;
}
