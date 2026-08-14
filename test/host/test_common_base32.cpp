/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "base32.hpp"

// Reference vectors computed with Python's stdlib `base64.b32encode()`
// (RFC 4648 Base32, the same alphabet/algorithm this module implements),
// not hand-derived -- avoids the exact class of "invented value" mistake
// this project's own discipline keeps catching by checking against real
// tools instead of memory.

int main() {
    // Empty input -> empty string.
    {
        char out[1]{};
        assert(common::base32_encode(nullptr, 0, out, sizeof(out)));
        assert(out[0] == '\0');
        assert(common::base32_encoded_length(0) == 0U);
    }

    // "Hello" (5 bytes -> 8 chars, no padding: 5*8=40 bits = 8*5 exactly).
    // python3: base64.b32encode(b'Hello') == b'JBSWY3DP'
    {
        const uint8_t input[5] = {'H', 'e', 'l', 'l', 'o'};
        char out[9]{};
        assert(common::base32_encoded_length(5) == 8U);
        assert(common::base32_encode(input, sizeof(input), out, sizeof(out)));
        assert(std::strcmp(out, "JBSWY3DP") == 0);
    }

    // bytes(range(10)) -> 16 chars, no padding -- the exact byte count
    // and character count this project's real provisioning-passphrase
    // generator uses. python3: base64.b32encode(bytes(range(10))) ==
    // b'AAAQEAYEAUDAOCAJ'
    {
        uint8_t input[10]{};
        for (uint8_t i = 0; i < 10; ++i) {
            input[i] = i;
        }
        char out[17]{};
        assert(common::base32_encoded_length(10) == 16U);
        assert(common::base32_encode(input, sizeof(input), out, sizeof(out)));
        assert(std::strcmp(out, "AAAQEAYEAUDAOCAJ") == 0);
    }

    // Buffer exactly one byte too small must fail without writing.
    {
        const uint8_t input[5] = {'H', 'e', 'l', 'l', 'o'};
        char out[8]{'X', 'X', 'X', 'X', 'X', 'X', 'X', '\0'};
        assert(!common::base32_encode(input, sizeof(input), out, sizeof(out)));
        assert(out[0] == 'X');  // untouched on failure
    }

    // Exact-fit buffer (required_len + 1) succeeds.
    {
        const uint8_t input[5] = {'H', 'e', 'l', 'l', 'o'};
        char out[9]{};
        assert(common::base32_encode(input, sizeof(input), out, sizeof(out)));
    }

    // Null out pointer is rejected.
    {
        const uint8_t input[1] = {0x00};
        assert(!common::base32_encode(input, sizeof(input), nullptr, 10));
    }

    // Null input with nonzero length is rejected.
    {
        char out[10]{};
        assert(!common::base32_encode(nullptr, 5, out, sizeof(out)));
    }

    return 0;
}
