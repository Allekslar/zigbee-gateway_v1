/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "ota_trust_store.hpp"

#include <cstring>

namespace service {
namespace {

bool strings_equal(const char* lhs, const char* rhs) noexcept {
    if (lhs == nullptr || rhs == nullptr) {
        return false;
    }
    return std::strcmp(lhs, rhs) == 0;
}

#ifdef ESP_PLATFORM
extern const char ota_release_manifest_pub_pem_start[] asm("_binary_ota_release_manifest_pub_pem_start");
extern const char ota_release_manifest_pub_pem_end[] asm("_binary_ota_release_manifest_pub_pem_end");
extern const char ota_release_manifest_pub_next_pem_start[] asm("_binary_ota_release_manifest_pub_next_pem_start");
extern const char ota_release_manifest_pub_next_pem_end[] asm("_binary_ota_release_manifest_pub_next_pem_end");

struct ManifestPublicKeyEntry {
    const char* key_id;
    const char* pem_start;
    const char* pem_end;
};

const ManifestPublicKeyEntry kManifestPublicKeys[] = {
    {
        .key_id = "ota-release-v1",
        .pem_start = ota_release_manifest_pub_pem_start,
        .pem_end = ota_release_manifest_pub_pem_end,
    },
    {
        .key_id = "ota-release-v2",
        .pem_start = ota_release_manifest_pub_next_pem_start,
        .pem_end = ota_release_manifest_pub_next_pem_end,
    },
};
#else
const char kManifestPublicKeyV1Pem[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "ota-release-v1-test-key\n"
    "-----END PUBLIC KEY-----\n";

const char kManifestPublicKeyV2Pem[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "ota-release-v2-test-key\n"
    "-----END PUBLIC KEY-----\n";
#endif

}  // namespace

const char* find_ota_manifest_public_key_pem(const char* signature_key_id) noexcept {
    if (signature_key_id == nullptr || signature_key_id[0] == '\0') {
        return nullptr;
    }

#ifdef ESP_PLATFORM
    for (std::size_t i = 0; i < (sizeof(kManifestPublicKeys) / sizeof(kManifestPublicKeys[0])); ++i) {
        if (strings_equal(signature_key_id, kManifestPublicKeys[i].key_id)) {
            const std::size_t pem_len =
                static_cast<std::size_t>(kManifestPublicKeys[i].pem_end - kManifestPublicKeys[i].pem_start);
            return pem_len > 1U ? kManifestPublicKeys[i].pem_start : nullptr;
        }
    }
    return nullptr;
#else
    if (strings_equal(signature_key_id, "ota-release-v1")) {
        return kManifestPublicKeyV1Pem;
    }
    if (strings_equal(signature_key_id, "ota-release-v2")) {
        return kManifestPublicKeyV2Pem;
    }
    return nullptr;
#endif
}

}  // namespace service
