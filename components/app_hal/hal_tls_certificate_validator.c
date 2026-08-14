/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#include "hal_tls_certificate_validator.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "log_tags.h"
#include "mbedtls/esp_mbedtls_random.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"

static const char* kTag = LOG_TAG_TLS_CERT_VALIDATOR;
#endif

hal_tls_cert_validation_result_t hal_tls_validate_certificate(
    const uint8_t* cert_pem, uint32_t cert_pem_len, const uint8_t* key_pem, uint32_t key_pem_len,
    const uint8_t* ca_pem, uint32_t ca_pem_len, const char* expected_dns_san, const char* expected_uri_san) {
#ifdef ESP_PLATFORM
    mbedtls_x509_crt cert;
    mbedtls_x509_crt ca;
    mbedtls_pk_context key;
    mbedtls_x509_crt_init(&cert);
    mbedtls_x509_crt_init(&ca);
    mbedtls_pk_init(&key);

    hal_tls_cert_validation_result_t result = HAL_TLS_CERT_VALIDATION_VALID;

    if (mbedtls_x509_crt_parse(&cert, cert_pem, cert_pem_len) != 0) {
        ESP_LOGE(kTag, "TLS certificate validation: certificate parse failed");
        result = HAL_TLS_CERT_VALIDATION_CERTIFICATE_PARSE_FAILED;
    } else if (mbedtls_x509_crt_parse(&ca, ca_pem, ca_pem_len) != 0) {
        ESP_LOGE(kTag, "TLS certificate validation: product CA parse failed");
        result = HAL_TLS_CERT_VALIDATION_CA_PARSE_FAILED;
    } else if (mbedtls_pk_parse_key(&key, key_pem, key_pem_len, NULL, 0, mbedtls_esp_random, NULL) != 0) {
        ESP_LOGE(kTag, "TLS certificate validation: private key parse failed");
        result = HAL_TLS_CERT_VALIDATION_PRIVATE_KEY_PARSE_FAILED;
    } else {
        /* Two independent mbedtls_x509_crt_verify() calls against the
         * same parsed chain -- one per required SAN value. Each call
         * independently re-checks chain-to-CA and expiry/not-yet-valid
         * (redundant the second time, but harmless and far simpler and
         * safer than hand-rolling a SAN-list walk -- see this module's
         * own header comment). */
        uint32_t dns_flags = 0;
        const int dns_rc = mbedtls_x509_crt_verify(&cert, &ca, NULL, expected_dns_san, &dns_flags, NULL, NULL);
        if (dns_rc != 0) {
            ESP_LOGE(
                kTag, "TLS certificate validation: chain/expiry/DNS-SAN check failed (rc=%d, flags=0x%08x)", dns_rc,
                (unsigned int)dns_flags);
            result = HAL_TLS_CERT_VALIDATION_CHAIN_OR_DNS_SAN_INVALID;
        } else {
            uint32_t uri_flags = 0;
            const int uri_rc = mbedtls_x509_crt_verify(&cert, &ca, NULL, expected_uri_san, &uri_flags, NULL, NULL);
            if (uri_rc != 0) {
                ESP_LOGE(
                    kTag, "TLS certificate validation: URI-SAN check failed (rc=%d, flags=0x%08x)", uri_rc,
                    (unsigned int)uri_flags);
                result = HAL_TLS_CERT_VALIDATION_URI_SAN_INVALID;
            } else if (mbedtls_pk_check_pair(&cert.pk, &key, mbedtls_esp_random, NULL) != 0) {
                ESP_LOGE(kTag, "TLS certificate validation: private key does not match certificate");
                result = HAL_TLS_CERT_VALIDATION_KEY_MISMATCH;
            }
        }
    }

    mbedtls_pk_free(&key);
    mbedtls_x509_crt_free(&ca);
    mbedtls_x509_crt_free(&cert);
    return result;
#else
    /* Host builds have zero crypto library dev headers available at all
     * (confirmed by direct recon, see admin_verifier.cpp's own top-of-
     * file comment) -- this module's real logic is ESP_PLATFORM-only,
     * verified only via a real idf.py build, matching hal_ota.c's own
     * pre-existing manifest-signature-verification precedent exactly.
     * Fails closed: a host build can never report a certificate valid. */
    (void)cert_pem;
    (void)cert_pem_len;
    (void)key_pem;
    (void)key_pem_len;
    (void)ca_pem;
    (void)ca_pem_len;
    (void)expected_dns_san;
    (void)expected_uri_san;
    return HAL_TLS_CERT_VALIDATION_CERTIFICATE_PARSE_FAILED;
#endif
}
