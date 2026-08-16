/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 Alex.K. */

#pragma once

#include <cstdint>

namespace web_ui {

// Plan S6 "HTTPS and sessions" #12 / FD-17: "...starts a bounded local
// verification using `next`..." -- this is that verification. Given a
// rotation candidate that has ALREADY passed offline X.509 validation
// (hal_tls_validate_certificate() -- chain/SAN/expiry/key-pairing, all
// static checks against the byte content alone), this function proves
// the candidate material also works in a REAL, live TLS handshake: it
// starts a temporary, minimal second httpd_ssl listener bound to the
// candidate cert/key on a non-production port
// (kCertRotationSelfTestPort), makes one real HTTPS request against it
// over loopback using esp_http_client (the same client machinery
// hal_ota.c already uses for real HTTPS downloads -- see
// docs/security/CONTROL_PLANE_SECURITY.md Section 2.15's own recon note
// on why this avoids needing new raw mbedtls client code), and tears the
// temporary listener down again regardless of the outcome. "Bounded":
// this function has one exit point after listener teardown in every
// code path -- no temporary listener is ever left running past this
// call's own return.
//
// The self-test client verifies the candidate's identity against
// `expected_dns_san` via esp_http_client_config_t's own `common_name`
// field -- a real, documented ESP-IDF mechanism for decoupling "which
// host the TCP connection targets" (127.0.0.1, loopback, no network or
// mDNS dependency) from "which identity the presented certificate must
// match" (this gateway's real production DNS SAN) -- so this genuinely
// exercises the same identity a real remote admin/browser client would
// check, not a weakened loopback-only check.
//
// ESP_PLATFORM-only real logic (temporary httpd_ssl + esp_http_client
// are both ESP-IDF platform APIs with no meaningful host equivalent,
// same class of boundary hal_tls_certificate_validator.h's own real
// X.509 logic already has). The host branch is defined but always
// returns false (fails closed) -- callers (the certificates/operations
// route handler) call this unconditionally, exactly like they already
// call hal_tls_validate_certificate() unconditionally, so no #ifdef is
// needed at any call site.
bool cert_rotation_bounded_self_test(
    const uint8_t* cert_pem, uint32_t cert_pem_len, const uint8_t* key_pem, uint32_t key_pem_len,
    const uint8_t* ca_pem, uint32_t ca_pem_len, const char* expected_dns_san) noexcept;

// Plan #12/FD-17: activation must survive "one successful reboot/post-
// activation check" -- a real device reboot, not an in-process re-init,
// is the literal mechanism this project's own OTA activation flow
// already uses for the analogous case. Schedules esp_restart() a short
// delay (kCertRotationRebootDelayMs) after this call returns, rather
// than calling it synchronously: the caller (the certificates/
// operations route handler) still needs to send its own HTTP response
// over the socket first -- an immediate esp_restart() would tear the
// connection down before the client ever saw a response. The delay is a
// one-shot esp_timer (ESP_TIMER_TASK dispatch, no new task/stack cost,
// same reasoning already established for the periodic button-poll timer
// in web_server.cpp), not a caller-visible synchronous sleep. A no-op on
// host (nothing to schedule; esp_restart() does not exist there).
void schedule_cert_rotation_reboot() noexcept;

}  // namespace web_ui
