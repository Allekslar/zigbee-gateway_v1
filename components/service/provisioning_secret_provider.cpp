/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "provisioning_secret_provider.hpp"

#include "hal_random.h"
#include "secure_storage_port.hpp"
#include "tls_provisioning_storage_port.hpp"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "log_tags.h"
#endif

namespace service {

namespace {

// No-op on host builds -- same established guarded-macro convention as
// secure_storage_port.cpp's SS_LOGI/SS_LOGW (see that file's own
// top-of-file comment for why).
#ifdef ESP_PLATFORM
constexpr const char* kTag = LOG_TAG_PROVISIONING_SECRET;
#define PSP_LOGI(...) ESP_LOGI(kTag, __VA_ARGS__)
#define PSP_LOGW(...) ESP_LOGW(kTag, __VA_ARGS__)
#else
#define PSP_LOGI(...) ((void)0)
#define PSP_LOGW(...) ((void)0)
#endif

constexpr char kHexDigits[] = "0123456789abcdef";

void format_hex(const uint8_t* bytes, uint32_t len, char* out, uint32_t out_capacity) {
    // Bounded internal helper: every call site below passes
    // kProvisioningSecretDevBytes, so 2*len+1 always fits the fixed
    // stack buffer the one call site declares.
    uint32_t i = 0;
    for (; i < len && (i * 2U + 1U) < out_capacity; ++i) {
        out[i * 2U] = kHexDigits[(bytes[i] >> 4) & 0x0FU];
        out[i * 2U + 1U] = kHexDigits[bytes[i] & 0x0FU];
    }
    out[i * 2U] = '\0';
}

#if defined(ESP_PLATFORM) && defined(CONFIG_ZGW_PRODUCTION_PROFILE) && CONFIG_ZGW_PRODUCTION_PROFILE
ProvisioningSecretStatus get_production_secret(ProvisioningSecret* out) noexcept {
    uint32_t value_len = 0U;
    const SecureStorageStatus status = manufacturing_provisioning_get_proof_of_possession(
        out->bytes, kProvisioningSecretMaxBytes, &value_len);
    if (status != SecureStorageStatus::kAvailable) {
        // Plan #2's own text: "production startup fails closed when
        // material is absent." Never falls back to generating one.
        PSP_LOGW("manufacturing PoP unavailable (status=%u) -- failing closed", static_cast<unsigned>(status));
        *out = ProvisioningSecret{};
        return ProvisioningSecretStatus::kUnavailable;
    }
    out->len = value_len;
    return ProvisioningSecretStatus::kAvailable;
}
#endif  // ESP_PLATFORM && CONFIG_ZGW_PRODUCTION_PROFILE

ProvisioningSecretStatus get_development_secret(ProvisioningSecret* out) noexcept {
    static_assert(
        kProvisioningSecretDevBytes <= kProvisioningSecretMaxBytes,
        "development secret must fit ProvisioningSecret::bytes");

    ProvisioningSecret secret{};
    if (hal_random_fill_bytes(secret.bytes, kProvisioningSecretDevBytes) != HAL_RANDOM_STATUS_OK) {
        *out = ProvisioningSecret{};
        return ProvisioningSecretStatus::kUnavailable;
    }
    secret.len = kProvisioningSecretDevBytes;

    // Development-only delivery channel: this device has no display,
    // only UART. Printing the freshly-generated one-time secret to the
    // serial console mirrors main/app_main.cpp's existing Wi-Fi AP
    // passphrase delivery posture (see that call site's own comment).
    char hex[kProvisioningSecretDevBytes * 2U + 1U]{};
    format_hex(secret.bytes, secret.len, hex, sizeof(hex));
    PSP_LOGI("Development provisioning secret (one-time, not persisted): %s", hex);

    *out = secret;
    return ProvisioningSecretStatus::kAvailable;
}

}  // namespace

ProvisioningSecretStatus provisioning_secret_provider_get(ProvisioningSecret* out) noexcept {
    if (out == nullptr) {
        return ProvisioningSecretStatus::kUnavailable;
    }

#if defined(ESP_PLATFORM) && defined(CONFIG_ZGW_PRODUCTION_PROFILE) && CONFIG_ZGW_PRODUCTION_PROFILE
    return get_production_secret(out);
#else
    return get_development_secret(out);
#endif
}

}  // namespace service
