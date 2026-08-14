/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <array>
#include <cassert>
#include <cstring>

#include "gateway_id.hpp"
#include "provisioning_secrets.hpp"
#include "security_bounds.hpp"

namespace {

common::GatewayId make_gateway_id(const std::array<uint8_t, common::GatewayId::kByteLength>& bytes) {
    return common::GatewayId(bytes);
}

void test_generate_provisioning_passphrase_has_exact_approved_length() {
    char passphrase[service::SecurityBounds::kProvisioningPassphraseBase32Chars + 1U]{};
    assert(service::generate_provisioning_passphrase(passphrase, sizeof(passphrase)));
    assert(std::strlen(passphrase) == service::SecurityBounds::kProvisioningPassphraseBase32Chars);
}

void test_generate_provisioning_passphrase_only_uses_the_base32_alphabet() {
    char passphrase[service::SecurityBounds::kProvisioningPassphraseBase32Chars + 1U]{};
    assert(service::generate_provisioning_passphrase(passphrase, sizeof(passphrase)));
    for (const char* c = passphrase; *c != '\0'; ++c) {
        const bool is_upper_letter = *c >= 'A' && *c <= 'Z';
        const bool is_digit_2_to_7 = *c >= '2' && *c <= '7';
        assert(is_upper_letter || is_digit_2_to_7);
    }
}

void test_generate_provisioning_passphrase_varies_between_calls() {
    char first[service::SecurityBounds::kProvisioningPassphraseBase32Chars + 1U]{};
    char second[service::SecurityBounds::kProvisioningPassphraseBase32Chars + 1U]{};
    assert(service::generate_provisioning_passphrase(first, sizeof(first)));
    assert(service::generate_provisioning_passphrase(second, sizeof(second)));
    assert(std::strcmp(first, second) != 0);
}

void test_generate_provisioning_passphrase_rejects_undersized_buffer() {
    char too_small[service::SecurityBounds::kProvisioningPassphraseBase32Chars]{};  // one short of +1
    assert(!service::generate_provisioning_passphrase(too_small, sizeof(too_small)));
}

void test_generate_random_secret_hex_has_correct_length_and_alphabet() {
    char secret[65]{};
    assert(service::generate_random_secret_hex(32U, secret, sizeof(secret)));
    assert(std::strlen(secret) == 64U);
    for (const char* c = secret; *c != '\0'; ++c) {
        const bool is_hex = (*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f');
        assert(is_hex);
    }
}

void test_generate_random_secret_hex_rejects_undersized_buffer() {
    char too_small[4]{};
    assert(!service::generate_random_secret_hex(4U, too_small, sizeof(too_small)));
}

void test_generate_random_secret_hex_rejects_zero_length() {
    char buffer[8]{};
    assert(!service::generate_random_secret_hex(0U, buffer, sizeof(buffer)));
}

void test_build_provisioning_ap_ssid_uses_last_six_hex_chars() {
    const common::GatewayId gateway_id = make_gateway_id({{0x00, 0x12, 0x4b, 0xaa, 0xbb, 0xcc}});
    char ssid[32]{};
    assert(service::build_provisioning_ap_ssid(gateway_id, "zigbee-gateway", ssid, sizeof(ssid)));
    // GatewayId formats to "00124baabbcc" (12 lowercase hex chars); the
    // last 6 are "aabbcc" -- matches plan #9's own last-6-hex convention
    // for the mDNS host.
    assert(std::strcmp(ssid, "zigbee-gateway-aabbcc") == 0);
}

void test_build_provisioning_ap_ssid_rejects_invalid_gateway_id() {
    const common::GatewayId invalid_gateway_id;  // default-constructed, all-zero -- invalid
    char ssid[32]{};
    assert(!service::build_provisioning_ap_ssid(invalid_gateway_id, "zigbee-gateway", ssid, sizeof(ssid)));
}

void test_build_provisioning_ap_ssid_rejects_null_or_empty_prefix() {
    const common::GatewayId gateway_id = make_gateway_id({{0x00, 0x12, 0x4b, 0xaa, 0xbb, 0xcc}});
    char ssid[32]{};
    assert(!service::build_provisioning_ap_ssid(gateway_id, nullptr, ssid, sizeof(ssid)));
    assert(!service::build_provisioning_ap_ssid(gateway_id, "", ssid, sizeof(ssid)));
}

void test_build_provisioning_ap_ssid_rejects_undersized_buffer() {
    const common::GatewayId gateway_id = make_gateway_id({{0x00, 0x12, 0x4b, 0xaa, 0xbb, 0xcc}});
    char too_small[10]{};  // "zigbee-gateway-aabbcc" needs 22 bytes
    assert(!service::build_provisioning_ap_ssid(gateway_id, "zigbee-gateway", too_small, sizeof(too_small)));
}

// build_gateway_mdns_host() always takes the host build's "development"
// branch (ESP_PLATFORM/CONFIG_ZGW_PRODUCTION_PROFILE are never defined
// here) -- these tests exercise exactly that: the plain, unsuffixed
// kGatewayHostNamePrefix, regardless of gateway_id. The production
// branch ("<prefix>-<last6>") is exercised only via a real
// ZGW_PRODUCTION_BUILD=1 idf.py build (see this sub-slice's evidence
// file), matching how provisioning_secret_provider.cpp's own
// production-only adapter is verified.

void test_build_gateway_mdns_host_is_the_plain_prefix_on_a_host_build() {
    const common::GatewayId gateway_id = make_gateway_id({{0x00, 0x12, 0x4b, 0xaa, 0xbb, 0xcc}});
    char host[32]{};
    assert(service::build_gateway_mdns_host(gateway_id, host, sizeof(host)));
    assert(std::strcmp(host, service::kGatewayHostNamePrefix) == 0);
}

void test_build_gateway_mdns_host_ignores_an_invalid_gateway_id_on_a_host_build() {
    const common::GatewayId invalid_gateway_id;  // default-constructed, all-zero -- invalid
    char host[32]{};
    // The development branch never reads gateway_id at all, so an
    // invalid one must not cause a failure the way
    // build_provisioning_ap_ssid()'s own production-relevant validity
    // check would.
    assert(service::build_gateway_mdns_host(invalid_gateway_id, host, sizeof(host)));
    assert(std::strcmp(host, service::kGatewayHostNamePrefix) == 0);
}

void test_build_gateway_mdns_host_rejects_undersized_buffer() {
    const common::GatewayId gateway_id = make_gateway_id({{0x00, 0x12, 0x4b, 0xaa, 0xbb, 0xcc}});
    char too_small[4]{};  // "zigbee-gateway" needs 15 bytes
    assert(!service::build_gateway_mdns_host(gateway_id, too_small, sizeof(too_small)));
}

void test_build_gateway_mdns_host_rejects_null_out() {
    const common::GatewayId gateway_id = make_gateway_id({{0x00, 0x12, 0x4b, 0xaa, 0xbb, 0xcc}});
    assert(!service::build_gateway_mdns_host(gateway_id, nullptr, 32));
}

void test_build_gateway_uri_san_has_the_exact_plan_named_format() {
    const common::GatewayId gateway_id = make_gateway_id({{0x00, 0x12, 0x4b, 0xaa, 0xbb, 0xcc}});
    char uri[32]{};
    assert(service::build_gateway_uri_san(gateway_id, uri, sizeof(uri)));
    assert(std::strcmp(uri, "urn:zgw:00124baabbcc") == 0);
}

void test_build_gateway_uri_san_rejects_invalid_gateway_id() {
    const common::GatewayId invalid_gateway_id;  // default-constructed, all-zero -- invalid
    char uri[32]{};
    assert(!service::build_gateway_uri_san(invalid_gateway_id, uri, sizeof(uri)));
}

void test_build_gateway_uri_san_rejects_undersized_buffer() {
    const common::GatewayId gateway_id = make_gateway_id({{0x00, 0x12, 0x4b, 0xaa, 0xbb, 0xcc}});
    char too_small[10]{};  // "urn:zgw:00124baabbcc" needs 21 bytes
    assert(!service::build_gateway_uri_san(gateway_id, too_small, sizeof(too_small)));
}

void test_build_gateway_uri_san_rejects_null_out() {
    const common::GatewayId gateway_id = make_gateway_id({{0x00, 0x12, 0x4b, 0xaa, 0xbb, 0xcc}});
    assert(!service::build_gateway_uri_san(gateway_id, nullptr, 32));
}

}  // namespace

int main() {
    test_generate_provisioning_passphrase_has_exact_approved_length();
    test_generate_provisioning_passphrase_only_uses_the_base32_alphabet();
    test_generate_provisioning_passphrase_varies_between_calls();
    test_generate_provisioning_passphrase_rejects_undersized_buffer();
    test_generate_random_secret_hex_has_correct_length_and_alphabet();
    test_generate_random_secret_hex_rejects_undersized_buffer();
    test_generate_random_secret_hex_rejects_zero_length();
    test_build_provisioning_ap_ssid_uses_last_six_hex_chars();
    test_build_provisioning_ap_ssid_rejects_invalid_gateway_id();
    test_build_provisioning_ap_ssid_rejects_null_or_empty_prefix();
    test_build_provisioning_ap_ssid_rejects_undersized_buffer();
    test_build_gateway_mdns_host_is_the_plain_prefix_on_a_host_build();
    test_build_gateway_mdns_host_ignores_an_invalid_gateway_id_on_a_host_build();
    test_build_gateway_mdns_host_rejects_undersized_buffer();
    test_build_gateway_mdns_host_rejects_null_out();
    test_build_gateway_uri_san_has_the_exact_plan_named_format();
    test_build_gateway_uri_san_rejects_invalid_gateway_id();
    test_build_gateway_uri_san_rejects_undersized_buffer();
    test_build_gateway_uri_san_rejects_null_out();
    return 0;
}
