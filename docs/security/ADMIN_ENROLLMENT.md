<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 Alex.K. -->

# Admin enrollment: setting the first administrator password

This is the operational runbook for `POST /api/v1/provisioning/enroll` --
the route that creates the very first administrator credential on a
device. It is the only way an admin password comes into existence: there
is no default password, no factory password and no recovery backdoor. If
enrollment never succeeds, the control plane has no administrator and
`POST /api/v1/auth/login` can never return a session.

The route's implementation is `provisioning_enroll_post_handler_v1()` in
`components/web_ui/web_handlers_auth.cpp`; the policy it enforces is
described in `docs/security/CONTROL_PLANE_SECURITY.md` Section 2.13.
This document adds what that section does not: the exact order the gates
run in, what each failure token means operationally, and the
preconditions that must already be in storage before the route can
succeed at all.

## 1. The gate chain

Every gate runs in this exact order. The order matters operationally,
and Section 1.2 explains why.

| # | Gate | Failure token | HTTP |
|---|------|---------------|------|
| 1 | Commissioning window is active | `provisioning_not_active` | 409 |
| 2 | Request body readable, <= 256 bytes | `invalid_request` | 400 |
| 3 | `proof_of_possession` + `admin_password` both present and non-empty | `invalid_request` | 400 |
| 4 | PoP hex decodes and matches stored secret | `unauthenticated` | 401 |
| 5 | **Physical-presence grant consumed** | `physical_presence_required` | 403 |
| 6 | GatewayId matches manufacturing record (production profile only) | `capability_unavailable` | 503 |
| 7 | Verifier created and written to storage | `capability_unavailable` | 503 |

On success the route returns `200 OK` and **closes the commissioning
window early** via `commissioning_window_stop()` -- the plan's own text:
*"an administrator enrolls and the window should close early."* A second
enrollment attempt therefore returns `provisioning_not_active` (409).
That 409-after-success is the confirmation signal, not an error.

### 1.1 Request shape

```
POST /api/v1/provisioning/enroll
Content-Type: application/json

{"proof_of_possession":"<hex>","admin_password":"<password>"}
```

Buffer limits from the handler, all of which reject rather than
truncate: whole body 256 bytes, `admin_password` 128 bytes, PoP hex
`kProvisioningSecretMaxBytes * 2 + 1` = 129 bytes (so up to 64 raw
bytes). A password long enough to push the body past 256 bytes fails as
`invalid_request`, not as a password error.

The route is deliberately **not** wrapped by
`register_authenticated_uri_handler_v1()` -- like login, it is reached
without an existing session, because it is what creates the first
session's credential.

### 1.2 Why the gate order is the thing to know

**Gate 5 consumes the physical-presence grant before gate 6 checks
identity.** A grant is single-use and lives 60 seconds
(`kPhysicalPresenceGrantMaxLifetimeSeconds`). So if gate 6 fails, the
button press is already spent: the operator must press the button again
for the next attempt. During HIL bring-up this cost a full press --
attempt 1 returned `capability_unavailable` (503) rather than
`physical_presence_required` (403), and that difference was the evidence
that the grant had been valid and consumed, with the failure one step
further along.

Practical consequence: **before asking anyone to press the button,
confirm gates 1-4 pass.** Send the real request with the real PoP and no
grant. A `403 physical_presence_required` proves the window is open, the
body parses and the PoP matches -- the press is then the only missing
input. Any other token means fix that first; a press now would be
wasted.

## 2. Preconditions that must already be in storage

Two records must exist in the `kManufacturingProvisioning` NVS namespace
before the route can reach gate 7. Both are normally written by a
manufacturing step that this repository does not have -- S5 recorded it
as `BLOCKED_SECURITY_PROVISIONING`, no manufacturing/eFuse environment
exists here -- so on a bench device they must be provisioned by hand.

| Key | Bytes | Written by | Gate |
|-----|-------|-----------|------|
| `mfg_pop` | up to 64 | `manufacturing_provisioning_set_proof_of_possession()` | 4 |
| `mfg_gateway_id` | 6 | `set_stored_manufacturing_gateway_id()` | 6 |

`mfg_gateway_id` is the whole "manufacturing record" gate 6 checks: the
raw 6 GatewayId bytes, compared against the value the firmware reads
live from its own factory base MAC. It is **not** the eFuse-provisioning
-template JSON, despite the adjacent
`manufacturing_provisioning_*_efuse_record()` API in
`tls_provisioning_storage_port.hpp` -- `gateway_identity_verification.hpp`
says so explicitly ("deliberately NOT the full eFuse-provisioning
-template JSON record"). Reaching for the eFuse API here is a dead end.

Gate 6 fails closed: `kNoManufacturingRecord` is treated exactly like
`kMismatch`. Absence of manufacturing evidence is never implicit proof
of authenticity. Because nothing in a real deployment populates the
record yet, `gateway_id_verification_allows_production_enrollment()`
returns false by default on production builds -- documented behaviour,
not a defect.

Gate 6 is compiled out entirely when `CONFIG_ZGW_PRODUCTION_PROFILE` is
not set. Development builds skip it: no manufacturing record is
populated there either, and development already carries weaker trust
guarantees throughout.

### 2.1 Provisioning them on a bench device

There is no route or console command for this -- by design, since a
route that writes the manufacturing record would defeat the anti-cloning
check it feeds. The bench method is a temporary hook in
`main/app_main.cpp`, placed after the runtime has started (so
`g_runtime.gateway_id()` is populated) and before the web server starts:

```cpp
// [HIL-DIAG] TEMPORARY -- delete after the first successful boot.
{
    static const uint8_t kPop[32] = { /* ... */ };
    manufacturing_provisioning_set_proof_of_possession(kPop, sizeof(kPop));
    set_stored_manufacturing_gateway_id(g_runtime.gateway_id());
}
```

Both writes are idempotent and land in encrypted NVS, which survives an
app-partition reflash. **Remove the hook and reflash as soon as the
records are written.** Left in place it rewrites both records on every
boot, which turns the anti-cloning check into a no-op: a cloned image
would simply re-record whatever MAC it found itself running on.

## 3. Procedure

Ordering is the whole difficulty. A reboot opens a fresh commissioning
window but also clears any outstanding presence grant, so **reboot
first, press second** -- the reverse throws the press away.

1. **Provision the records** (Section 2.1), reflash, confirm from the
   boot log that both writes returned `0`.
2. **Reboot** to open a fresh window. Default lifetime is 600 s
   (`kCommissioningWindowSeconds`, range 60-600, in
   `components/service/security_bounds.cpp`).
3. **Dry-run the request** with no grant. Expect
   `403 physical_presence_required`. Anything else: fix it before
   involving the button.
4. **Press the BOOT button** (GPIO 9 on the ESP32-C6-DevKitC-1;
   `hal_button.h`). `web_server.cpp` turns the event into a grant via
   `physical_presence_grant_create_from_button()`.
5. **Send the request** within 60 s.

Step 5 does not have to be timed by hand. Because a request without a
grant fails harmlessly at gate 5 and consumes nothing, a poll loop can
be started *before* the press and will convert the grant the moment it
appears. There is no rate limiting on this route today (S6 #28 is not
started -- see `CONTROL_PLANE_SECURITY.md` Section 3), so polling is
safe; note that this is also a real brute-force exposure the plan tracks
separately.

The loop's own exit condition is worth getting right: a successful
enrollment closes the window, so the *next* poll returns
`409 provisioning_not_active`. Treat that as success-then-closed rather
than failure -- during bring-up the success response itself was lost to
a dropped connection and the 409 was the only surviving evidence, with
the device-side `set_blob ok, key='admin_verifier' len=52` log line
confirming it.

## 4. Verifying

```
POST /api/v1/auth/login      {"password":"<password>"}
  -> 200 {"schema_version":1,"logged_in":true}
  -> Set-Cookie: zgw_session=...; Secure; HttpOnly; SameSite=Strict; Path=/api/v1

GET  /api/v1/auth/session    Cookie: zgw_session=...
  -> 200 {"schema_version":1,"csrf_token":"...","capabilities":[...]}
```

A freshly enrolled administrator holds every capability in the taxonomy:
`read_status`, `control_device`, `manage_network`, `commission_device`,
`remove_device`, `firmware_admin`, `security_admin`, `factory_reset`.

The login route is `/api/v1/auth/login`, not `/api/v1/auth/sessions`.
The password is checked against the stored verifier only; the route
never distinguishes wrong-password from no-administrator-enrolled.

### 4.1 Client-side gotchas on a production build

- **TLS 1.2 only.** `CONFIG_MBEDTLS_SSL_PROTO_TLS1_2` is compiled in and
  1.3 is not, so clients must cap at 1.2 (`curl --tls-max 1.2`).
- **Certificate name** is `zigbee-gateway-<last-6-of-mac>.local` with a
  matching DNS SAN plus `URI:urn:zgw:<gateway-id>`; derived in
  `build_gateway_mdns_host()`. Address the device by that name and pin
  the resolution (`curl --resolve <name>:443:<ip>`) rather than
  connecting to the bare IP, which will fail hostname verification.
- **Serial capture reboots the board.** Opening the port with a tool
  that asserts DTR/RTS resets the device, which closes the commissioning
  window and clears any grant. During bring-up this artifact also
  produced false "flaky TLS" readings, because closing the port mid
  -request rebooted the device. Use a non-asserting reader (`stty` +
  `cat`) when observing without wanting a reset.

## 5. Recovery

There is none in the credential itself. If the password is lost, the
verifier can only be replaced by erasing the `admin_verifier` key from
encrypted NVS -- which requires either physical flash access or the S8
factory-reset path, and S8 does not exist yet (`factory-reset/operations`
today validates policy and returns without erasing anything; see
`CONTROL_PLANE_SECURITY.md` Section 2.13). Plan accordingly on a device
whose flash encryption is already burned.

## 6. Known-unresolved: intermittent TLS handshake failures

Measured on real hardware after enrollment: roughly one connection in
five is refused mid-handshake.

```
client: curl: (35) TLS connect error: ... unexpected eof while reading
device: E esp-tls-mbedtls: mbedtls_ssl_handshake returned -0x0050
        E esp_https_server: esp_tls_create_server_session failed, 0x0050
        E httpd: httpd_accept_conn: session creation failed
```

`-0x0050` is `MBEDTLS_ERR_NET_RECV_FAILED` -- the peer went away during
the handshake. The suspected mechanism is `web_server.cpp`'s
`max_open_sockets = 4` combined with `lru_purge_enable = true` and
`keep_alive_idle = 5`: a fifth connection evicts the least-recently-used
socket, and an eviction landing on an in-progress handshake would
produce exactly this. **This is a hypothesis and has not been tested** --
neither the socket limit nor the purge setting has been varied.

Operationally: retry. The failure is at connection setup, so a retried
request is not a duplicate operation. This matters most for the poll
loop in Section 3, where a lost success response is indistinguishable
from a failed attempt until the following `409 provisioning_not_active`
arrives.
