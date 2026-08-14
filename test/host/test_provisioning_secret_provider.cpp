/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include <cassert>
#include <cstring>

#include "provisioning_secret_provider.hpp"

// Host builds always take the development adapter branch (ESP_PLATFORM
// is never defined here, so CONFIG_ZGW_PRODUCTION_PROFILE is never
// checked) -- this file exercises exactly that: fresh, ungenerated
// one-time secrets, never a real manufacturing proof-of-possession
// value. The production adapter path (real
// manufacturing_provisioning_get_proof_of_possession() reads) can only
// be exercised on a real ESP_PLATFORM build; see this sub-slice's
// evidence file for the real-target build confirming it compiles and
// links.

namespace {

using service::ProvisioningSecret;
using service::ProvisioningSecretStatus;

void test_provisioning_secret_provider_get_returns_available_with_dev_length() {
    ProvisioningSecret secret{};
    assert(service::provisioning_secret_provider_get(&secret) == ProvisioningSecretStatus::kAvailable);
    assert(secret.len == service::kProvisioningSecretDevBytes);
}

void test_provisioning_secret_provider_get_varies_between_calls() {
    ProvisioningSecret first{};
    ProvisioningSecret second{};
    assert(service::provisioning_secret_provider_get(&first) == ProvisioningSecretStatus::kAvailable);
    assert(service::provisioning_secret_provider_get(&second) == ProvisioningSecretStatus::kAvailable);
    assert(std::memcmp(first.bytes, second.bytes, first.len) != 0);
}

void test_provisioning_secret_provider_get_rejects_null_out() {
    assert(service::provisioning_secret_provider_get(nullptr) == ProvisioningSecretStatus::kUnavailable);
}

}  // namespace

int main() {
    test_provisioning_secret_provider_get_returns_available_with_dev_length();
    test_provisioning_secret_provider_get_varies_between_calls();
    test_provisioning_secret_provider_get_rejects_null_out();
    return 0;
}
