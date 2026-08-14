/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "session_security_policy.hpp"

#include <cstdio>
#include <cstring>

namespace service {

namespace {

// ASCII-only case-insensitive byte compare -- origins are ASCII
// (scheme://host[:port], RFC 6454) so this is deliberately not a
// locale-aware <cctype> tolower(); no existing case-insensitive compare
// convention exists anywhere else in this repository to match, so this
// is written as a small local helper, matching admin_verifier.cpp's own
// small local rotr()-style helper precedent.
uint8_t ascii_lower(uint8_t c) noexcept {
    return (c >= 'A' && c <= 'Z') ? (uint8_t)(c - 'A' + 'a') : c;
}

bool ascii_case_insensitive_equal(const char* a, const char* b) noexcept {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    while (*a != '\0' && *b != '\0') {
        if (ascii_lower((uint8_t)*a) != ascii_lower((uint8_t)*b)) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == *b;  // both must have reached '\0' together
}

}  // namespace

bool build_session_cookie_header(const char* session_id_hex, char* out, uint32_t out_capacity) noexcept {
    if (session_id_hex == nullptr || session_id_hex[0] == '\0' || out == nullptr) {
        return false;
    }
    const int written = std::snprintf(
        out, out_capacity, "%s=%s; Secure; HttpOnly; SameSite=Strict; Path=%s", kSessionCookieName, session_id_hex,
        kSessionCookiePath);
    return written > 0 && static_cast<uint32_t>(written) < out_capacity;
}

bool build_session_cookie_clear_header(char* out, uint32_t out_capacity) noexcept {
    if (out == nullptr) {
        return false;
    }
    const int written = std::snprintf(
        out, out_capacity, "%s=; Secure; HttpOnly; SameSite=Strict; Path=%s; Max-Age=0", kSessionCookieName,
        kSessionCookiePath);
    return written > 0 && static_cast<uint32_t>(written) < out_capacity;
}

bool session_csrf_token_matches(
    const SessionStoreState& state, const char* session_id_hex, const char* csrf_token_hex_from_request,
    uint64_t now_ms) noexcept {
    if (csrf_token_hex_from_request == nullptr || std::strlen(csrf_token_hex_from_request) != kCsrfTokenHexChars) {
        return false;
    }

    char stored_csrf_token[kCsrfTokenHexChars + 1U]{};
    if (!session_store_get_csrf_token(state, session_id_hex, now_ms, stored_csrf_token, sizeof(stored_csrf_token))) {
        return false;
    }

    // Constant-time compare -- never short-circuits on the first
    // mismatched byte, matching admin_verifier.cpp's password-hash
    // comparison discipline.
    uint8_t diff = 0U;
    for (uint32_t i = 0; i < kCsrfTokenHexChars; ++i) {
        diff = (uint8_t)(diff | ((uint8_t)stored_csrf_token[i] ^ (uint8_t)csrf_token_hex_from_request[i]));
    }
    return diff == 0U;
}

bool is_same_origin_request(const char* request_origin, const char* expected_origin) noexcept {
    if (expected_origin == nullptr || expected_origin[0] == '\0') {
        return false;
    }
    if (request_origin == nullptr || request_origin[0] == '\0') {
        // No Origin header at all -- fail closed, no benefit-of-the-doubt
        // pass (plan #16: "default to same-origin only").
        return false;
    }
    return ascii_case_insensitive_equal(request_origin, expected_origin);
}

bool cors_cross_origin_allowed() noexcept {
    return false;  // plan #16: "Reject permissive CORS; default to same-origin only."
}

}  // namespace service
