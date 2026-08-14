/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "session_security_policy.hpp"
#include "session_store.hpp"

namespace {

using service::SessionCreateResult;
using service::SessionRecord;
using service::SessionStoreState;

void test_build_session_cookie_header_has_the_exact_plan_named_attributes() {
    char out[256]{};
    assert(service::build_session_cookie_header("abc123", out, sizeof(out)));
    assert(std::strcmp(out, "zgw_session=abc123; Secure; HttpOnly; SameSite=Strict; Path=/api/v1") == 0);
}

void test_build_session_cookie_header_rejects_null_or_empty_id() {
    char out[256]{};
    assert(!service::build_session_cookie_header(nullptr, out, sizeof(out)));
    assert(!service::build_session_cookie_header("", out, sizeof(out)));
}

void test_build_session_cookie_header_rejects_undersized_buffer() {
    char too_small[5]{};
    assert(!service::build_session_cookie_header("abc123", too_small, sizeof(too_small)));
}

void test_build_session_cookie_clear_header_expires_immediately() {
    char out[256]{};
    assert(service::build_session_cookie_clear_header(out, sizeof(out)));
    assert(std::strcmp(out, "zgw_session=; Secure; HttpOnly; SameSite=Strict; Path=/api/v1; Max-Age=0") == 0);
}

void test_build_session_cookie_clear_header_rejects_undersized_buffer() {
    char too_small[5]{};
    assert(!service::build_session_cookie_clear_header(too_small, sizeof(too_small)));
}

void test_csrf_token_matches_the_session_bound_value() {
    SessionStoreState state{};
    SessionRecord record{};
    assert(service::session_store_create(&state, 0ULL, &record) == SessionCreateResult::kCreated);
    assert(service::session_csrf_token_matches(state, record.session_id_hex, record.csrf_token_hex, 0ULL));
}

void test_csrf_token_rejects_wrong_token() {
    SessionStoreState state{};
    SessionRecord record{};
    assert(service::session_store_create(&state, 0ULL, &record) == SessionCreateResult::kCreated);

    char wrong_token[service::kCsrfTokenHexChars + 1U]{};
    std::memset(wrong_token, 'a', service::kCsrfTokenHexChars);
    // Extremely unlikely to collide with a freshly-random real token, but
    // force a guaranteed mismatch on the first byte regardless.
    wrong_token[0] = (record.csrf_token_hex[0] == 'a') ? 'b' : 'a';
    assert(!service::session_csrf_token_matches(state, record.session_id_hex, wrong_token, 0ULL));
}

void test_csrf_token_rejects_wrong_length() {
    SessionStoreState state{};
    SessionRecord record{};
    assert(service::session_store_create(&state, 0ULL, &record) == SessionCreateResult::kCreated);
    assert(!service::session_csrf_token_matches(state, record.session_id_hex, "tooshort", 0ULL));
    assert(!service::session_csrf_token_matches(state, record.session_id_hex, nullptr, 0ULL));
}

void test_csrf_token_rejects_unknown_or_expired_session() {
    SessionStoreState state{};
    SessionRecord record{};
    assert(service::session_store_create(&state, 0ULL, &record) == SessionCreateResult::kCreated);

    assert(!service::session_csrf_token_matches(state, "unknown-session", record.csrf_token_hex, 0ULL));

    const uint64_t after_idle_timeout = (uint64_t)service::kSessionIdleTimeoutSeconds * 1000ULL;
    assert(!service::session_csrf_token_matches(
        state, record.session_id_hex, record.csrf_token_hex, after_idle_timeout));
}

void test_is_same_origin_request_exact_match() {
    assert(service::is_same_origin_request(
        "https://zigbee-gateway-abcdef.local", "https://zigbee-gateway-abcdef.local"));
}

void test_is_same_origin_request_case_insensitive() {
    assert(service::is_same_origin_request(
        "HTTPS://Zigbee-Gateway-ABCDEF.local", "https://zigbee-gateway-abcdef.local"));
}

void test_is_same_origin_request_rejects_different_origin() {
    assert(!service::is_same_origin_request("https://evil.example", "https://zigbee-gateway-abcdef.local"));
}

void test_is_same_origin_request_fails_closed_on_missing_origin_header() {
    assert(!service::is_same_origin_request(nullptr, "https://zigbee-gateway-abcdef.local"));
    assert(!service::is_same_origin_request("", "https://zigbee-gateway-abcdef.local"));
}

void test_is_same_origin_request_fails_closed_on_missing_expected_origin() {
    assert(!service::is_same_origin_request("https://zigbee-gateway-abcdef.local", nullptr));
    assert(!service::is_same_origin_request("https://zigbee-gateway-abcdef.local", ""));
}

void test_cors_cross_origin_never_allowed() {
    assert(!service::cors_cross_origin_allowed());
}

}  // namespace

int main() {
    test_build_session_cookie_header_has_the_exact_plan_named_attributes();
    test_build_session_cookie_header_rejects_null_or_empty_id();
    test_build_session_cookie_header_rejects_undersized_buffer();
    test_build_session_cookie_clear_header_expires_immediately();
    test_build_session_cookie_clear_header_rejects_undersized_buffer();
    test_csrf_token_matches_the_session_bound_value();
    test_csrf_token_rejects_wrong_token();
    test_csrf_token_rejects_wrong_length();
    test_csrf_token_rejects_unknown_or_expired_session();
    test_is_same_origin_request_exact_match();
    test_is_same_origin_request_case_insensitive();
    test_is_same_origin_request_rejects_different_origin();
    test_is_same_origin_request_fails_closed_on_missing_origin_header();
    test_is_same_origin_request_fails_closed_on_missing_expected_origin();
    test_cors_cross_origin_never_allowed();
    return 0;
}
