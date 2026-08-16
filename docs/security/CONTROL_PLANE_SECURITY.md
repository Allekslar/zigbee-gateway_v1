<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 Alex.K. -->

# Production hardening: control-plane security

This document records Stage S6 (`docs/implementation/PRODUCTION_HARDENING_PLAN.md`,
"Secure management, provisioning, strict JSON and MQTT transport"): what
is frozen and tested today, and what remains explicitly out of scope. It
is the security-focused counterpart to `docs/security/PRODUCTION_HARDENING.md`
(S5) and the `docs/architecture/*.md` series covering S4.

## 1. Why S6 is scoped into many sub-slices

S6's own required-changes list spans 37 numbered items across six very
different concerns -- provisioning/credentials, HTTPS/sessions,
authorization/physical presence, strict JSON parsing, rate limiting/audit,
and MQTT transport security -- each large enough to be its own coherent
unit of work, matching the sub-slicing discipline S5 used across its ten
"Encrypted storage foundation" items. The plan's own text orders one
cluster before all the others: *"Before implementing the control-plane
changes, create the Kconfig definitions and one typed security-bounds
accessor consumed by provisioning, Web parsing, rate limiting and audit
storage."* This document's Section 2.1 is exactly that foundation --
nothing else in S6 has started yet.

## 2. What is implemented and verified (sub-slices 1-13: security invariants/bounded tunables/typed accessor, provisioning AP secret generation, provisioning-credentials remainder, session store + cookie/CSRF/CORS policy, production HTTPS listener, production mDNS host derivation, gateway identity self-consistency verification, TLS certificate chain/SAN/expiry/key validation, central authorization middleware + capability taxonomy + login/logout/session, physical-presence grant primitive, auth/password + provisioning/enroll + factory-reset policy stub, real button-to-grant wiring with HIL confirmation, certificate rotation active-slot state machine + bounded self-test + route (compile/link-verified, HIL pending) -- plan #1-23 in full; plus a live HIL round that found and fixed four real boot-time task/stack defects, then two separate `.bss` starvation bugs that had been blocking Wi-Fi association and every TLS session, ending in the first authenticated control-plane exercise on real hardware -- a complete administrator enrollment and login, Sections 2.16-2.18; the certificate-rotation route answered `401 unauthenticated` over the network but a full authenticated rotation is still not driven)

### 2.1 `main/Kconfig.projbuild`'s new "Security" submenu + `security_bounds.hpp`/`.cpp`

Plan text: *"Before implementing the control-plane changes, create the
Kconfig definitions and one typed security-bounds accessor consumed by
provisioning, Web parsing, rate limiting and audit storage... The typed
accessor is the only application-facing source for tunables;
handlers/adapters must not repeat numeric literals."*

**Ten new Kconfig symbols**, each declared with a real `range MIN MAX`
and the plan's own approved default, added to a new "Security" submenu in
`main/Kconfig.projbuild` (existing menu/style conventions followed
exactly -- this repository already had real precedent for `range`-bounded
int symbols, e.g. `ZGW_OTA_BOOT_CONFIRM_TIMEOUT_MS`, `ZGW_MQTT_KEEPALIVE_SEC`):

| Symbol | Range | Approved default |
|---|---|---|
| `ZGW_COMMISSIONING_WINDOW_SECONDS` | 60-600 | 600 |
| `ZGW_JSON_MAX_BODY_BYTES` | 512-2048 | 2048 |
| `ZGW_JSON_MAX_DEPTH` | 2-4 | 4 |
| `ZGW_JSON_MAX_STRING_BYTES` | 64-512 | 512 |
| `ZGW_JSON_MAX_KEYS` | 8-32 | 32 |
| `ZGW_LOGIN_ATTEMPTS_PER_MINUTE` | 1-5 | 5 |
| `ZGW_COMMANDS_PER_MINUTE` | 10-60 | 60 |
| `ZGW_MUTATIONS_PER_MINUTE` | 1-5 | 5 |
| `ZGW_FIRMWARE_OPS_PER_HOUR` | 1-2 | 2 |
| `ZGW_AUDIT_RING_RECORDS` | 32-128 | 128 |

Two related values are explicitly **not** Kconfig symbols at all, per the
plan's own text: *"The provisioning passphrase length remains a
non-configurable 16 Base32 characters, and login backoff remains a
non-configurable 2-second start with a 60-second maximum."* These are
fixed `constexpr` members of `SecurityBounds`
(`kProvisioningPassphraseBase32Chars`, `kLoginBackoffStartSeconds`,
`kLoginBackoffMaxSeconds`) -- there is no menuconfig entry a build could
set differently even by mistake.

**`components/service/include/security_bounds.hpp`/`.cpp`** (new): the
typed accessor itself. `security_bounds()` returns a `const SecurityBounds&`
populated once from the real `CONFIG_ZGW_*` macros on `ESP_PLATFORM`, or
from the same approved-default values on a host build (there is no real
Kconfig on host at all -- this lets every later S6 host-tested component
build and test its bounds-respecting logic without a real ESP-IDF
`sdkconfig`, matching how `config_manager.hpp`'s own default constants are
already plain C++ values independent of any Kconfig source).
`security_bound_range(field)`/`security_bound_value_in_range(field, value)`
expose the same (minimum, maximum, approved_default) table the production
verifier (Section 2.2) also uses, as a runtime-callable predicate rather
than a value only ever read once at startup.

**This is scaffolding, matching S5's own established discipline carried
into S6**: nothing in this repository calls `security_bounds()` yet.
Provisioning, strict JSON parsing, rate limiting and audit storage --
the four consumers the plan names -- are each a separate, later S6
sub-slice.

### 2.2 `scripts/verify_production_security_profile.py` extended: tunable range verification (plan text: "the production verifier ... verifies tunable range membership")

The **same** production verifier S5 built and grew across its own
sub-slices (Kconfig symbols in Section 2.1 of `PRODUCTION_HARDENING.md`,
the ESP-IDF version check, the coredump-DRAM-capture forbidden symbol) now
also checks all ten S6 tunables. `TUNABLE_RANGES` declares the same
(symbol, minimum, maximum, approved_default) triples as
`security_bounds.hpp`'s `kRanges` table and `main/Kconfig.projbuild`'s
`range`/`default` declarations -- kept in sync by hand across all three,
a named, tracked responsibility (not automated; see the deviations list
below).

`verify_tunable_ranges()` rejects a tunable that is **absent**,
**unparseable**, or **outside** `[minimum, maximum]` -- but explicitly
does **not** reject a value that merely differs from the approved
default, per the plan's own carve-out: *"An in-range change from the
approved default requires a reviewed release-manifest delta and rerunning
the named affected tests, rather than an automatic rejection solely for
differing from the default."*

`sdkconfig.production.esp32c6` (S5's production profile fragment) now
also pins all ten tunables explicitly at their approved defaults --
redundant with the Kconfig-declared defaults themselves, but keeps the
approved profile visible in one place rather than silently depending on
an implicit default a future `main/Kconfig.projbuild` edit could change
without this file noticing.

**Real-tool verification, not just documentation**: a full
`ZGW_PRODUCTION_BUILD=1 idf.py build` inside `espressif/idf:release-v5.5`
confirmed all ten symbols appear in the real generated `sdkconfig` with
their approved-default values, and `verify_production_security_profile.py`
against that real file reported `OK`. A second, targeted check proved the
Kconfig `range` directive is not just documentation: editing the real
generated `sdkconfig` to set `CONFIG_ZGW_JSON_MAX_DEPTH=999` (far above
its declared maximum of 4) and running `idf.py reconfigure` produced a
real warning from ESP-IDF's own Kconfig tooling --

```
warning: user value 999 on the int symbol ZGW_JSON_MAX_DEPTH (defined at
/workspace/main/Kconfig.projbuild:223) ignored due to being outside the
active range ([2, 4]) -- falling back on defaults
```

-- and the regenerated `sdkconfig` reverted the value to `4`, the
approved default, exactly matching the plan's own text ("Kconfig rejects
out-of-range values") as an observed, real behavior rather than an
assumption.

### 2.3 Tests

**Python** (`scripts/test_verify_production_security_profile.py`, 8 new
tests, `VerifyTunableRangesTest`): the approved fixture's tunables all
pass; every one of the ten tunables passes at its exact minimum and
maximum (looped over `TUNABLE_RANGES`, not hand-written per symbol); every
one fails one-below-minimum and one-above-maximum (same loop); an
in-range non-default value passes (the plan's own carve-out, checked
directly); a missing tunable fails; an unparseable value fails. 38/38
passing in this script overall.

**C++** (`test/host/test_security_bounds.cpp`, new, 5 tests): the host
fallback matches the approved defaults exactly; the two fixed
non-configurable constants have the plan's own exact values; `security_bounds()`
returns a stable reference across calls (populated once, not
re-read/re-allocated); `security_bound_range()` matches the plan's table
for all ten fields, checked against the plan text directly rather than
against whatever the function itself returns (avoiding a tautological
test); `security_bound_value_in_range()` is checked at and around one
representative boundary.

Full repository host suite: **92/92** passing (91 pre-existing from S5 +
this sub-slice's new test executable) via `zgw-host-tools:s0`. `cppcheck
--enable=warning,performance,portability` via `zgw-host-tools:cppcheck`
reports zero findings against `security_bounds.cpp`. `check_arch_invariants.sh`
reports `high=0, medium=0, low=0`, unchanged.

### 2.4 `hal_random.h`/`.c` + `base32.hpp`/`.cpp` + `provisioning_secrets.hpp`/`.cpp` -- provisioning AP secret generation (plan #1, #4, #6)

Plan text: *"#1: Remove `kProvisioningApPassword = '12345678'` and every
shared/default production secret."* *"#4: Provisioning AP SSID includes a
non-secret gateway suffix; its passphrase is exactly 16 cryptographically
random Base32 characters and uses WPA2/WPA3 settings supported by
target."* *"#6: Generate session and CSRF secrets from hardware RNG."*

This is the **first real production wiring change** since S5 began --
every prior S5/S6 sub-slice added fully tested scaffolding with zero
production callers. `main/app_main.cpp` no longer has a
`kProvisioningApPassword` constant at all.

**`components/app_hal/include/hal_random.h`/`.c`** (new): `hal_random_fill_bytes()`
wraps ESP-IDF's real `esp_fill_random()` (`esp_hw_support/include/esp_random.h`,
confirmed against the real header inside `espressif/idf:release-v5.5`
before writing any code). Its own documentation states the *exact*
condition for true randomness -- *"If Wi-Fi or Bluetooth are enabled,
this function returns true random numbers. In other situations... consult
the ESP-IDF Programming Guide... for necessary prerequisites"* -- quoted
directly rather than overclaiming an unconditional guarantee; this
project's one real call site (`main/app_main.cpp`) generates the
passphrase only after the Wi-Fi subsystem is already being brought up to
start the AP itself, satisfying the condition. Host builds fall back to a
plain, explicitly non-cryptographic PRNG (seeded once per process) purely
so host tests can observe real variation between calls -- no production
code path ever executes the host branch.

**`components/common/include/base32.hpp`/`.cpp`** (new): unpadded RFC
4648 Base32 encoding, a pure formatting utility with no ESP-IDF
dependency (matching `GatewayId`'s own component boundary). Verified
against reference vectors computed with Python's stdlib
`base64.b32encode()` -- not hand-derived -- including the exact 10-byte
input shape this project's own passphrase generator uses
(`bytes(range(10))` -> `"AAAQEAYEAUDAOCAJ"`).

**`components/service/include/provisioning_secrets.hpp`/`.cpp`** (new):
`generate_provisioning_passphrase()` draws exactly 10 random bytes (=80
bits=exactly 16 Base32 characters, no padding, enforced by a
`static_assert` tying the byte count to `SecurityBounds::kProvisioningPassphraseBase32Chars`
from Section 2.1) and Base32-encodes them. `generate_random_secret_hex()`
is the reusable hex-secret primitive plan #6 names -- the actual
session/CSRF *subsystem* that will call it does not exist yet (a later
"HTTPS and sessions" sub-slice). `build_provisioning_ap_ssid()` builds
`"<prefix>-<last 6 hex chars of GatewayId>"`, reusing plan #9's own
"`<last6>`" suffix convention (named for the future mDNS host, not yet
implemented) for naming consistency.

**`components/app_hal/hal_wifi.c`** updated: `hal_wifi_start_ap()` now
sets `WIFI_AUTH_WPA2_WPA3_PSK` (ESP-IDF's real mixed-mode auth setting,
confirmed against `esp_wifi_types_generic.h`) instead of
`WIFI_AUTH_WPA2_PSK` -- satisfies plan #4's "WPA2/WPA3 settings
supported by target," accepting both WPA2-only and WPA3-capable admin
devices.

**`main/app_main.cpp`** wired: generates a fresh SSID and passphrase every
boot (never persisted -- matches "remove... every shared/default
production secret" being a removal, not a swap for a different fixed
value) and fails closed (halts with a logged error) if generation fails,
rather than falling back to any default. **Delivery channel, an explicit
user decision**: this device has no display, only UART -- the freshly
generated passphrase is logged via `ESP_LOGI` at AP start, the same
interim posture plan #2 explicitly sanctions for its own analogous case
("development adapter may generate and print a one-time secret"). A real
production channel (printed label, QR code, or another manufacturing
step) is a named, tracked follow-up, not designed here (Section 3.3).

**A real bug found only by the real ESP-IDF target build, not the host
build**: `app_main.cpp`'s first version referenced
`SecurityBounds::kProvisioningPassphraseBase32Chars` without the
`service::` namespace qualifier. This compiles nowhere in the host test
suite at all (`main/app_main.cpp` is not part of `test/host/CMakeLists.txt`
-- it is ESP-IDF-only application entry code), so only a real
`idf.py build` could have caught it, and did: a real compiler error
(`'SecurityBounds' has not been declared`). Fixed with the missing
`service::` qualifier, confirmed by a second, clean full build. This is
the sixth real defect this project's S5/S6 work has found only by
actually building against the real target, not the host toolchain or
review alone.

**Tests**: `test/host/test_hal_random.cpp` (4 tests: rejects null/zero-length
arguments; fills the requested length; two consecutive calls differ,
proving the host PRNG actually varies; a 1-byte boundary fill succeeds).
`test/host/test_common_base32.cpp` (7 tests, including both real
Python-computed reference vectors, an exact-fit buffer, an
one-byte-too-small buffer failing without writing, and null-argument
rejection). `test/host/test_provisioning_secrets.cpp` (11 tests: the
passphrase has the exact approved length and uses only the Base32
alphabet; two generated passphrases differ; undersized buffers are
rejected for all three functions; the hex-secret generator produces the
correct length/alphabet; the SSID builder uses exactly the last 6 hex
characters of a real-shaped `GatewayId` and rejects an invalid
`GatewayId`/null-or-empty prefix/undersized buffer).

Full repository host suite: **95/95** passing (92 pre-existing + 3 new
test executables) via `zgw-host-tools:s0`. `cppcheck --enable=warning,performance,portability`
via `zgw-host-tools:cppcheck` reports zero findings against all four
changed/new C/C++ files. `check_arch_invariants.sh` reports `high=0,
medium=0, low=0`, unchanged. A full real `idf.py build` inside
`espressif/idf:release-v5.5` succeeded end-to-end after the namespace-
qualifier fix above (confirming the new production wiring in
`main/app_main.cpp` actually compiles and links against the real target,
not just host-testable library code).

### 2.5 `admin_verifier.hpp`/`.cpp` + `provisioning_secret_provider.hpp`/`.cpp` + `commissioning_window.hpp`/`.cpp` -- provisioning-credentials remainder (plan #2, #3, #5)

Plan text: *"#2: Introduce a `ProvisioningSecretProvider` port: production
adapter reads per-device manufacturing proof-of-possession material;
development adapter may generate and print a one-time secret; production
startup fails closed when material is absent."* *"#3: Commissioning mode
starts only after first-boot policy or trusted physical button action and
expires according to `ZGW_COMMISSIONING_WINDOW_SECONDS`; approved default
is 600 seconds and every production value must remain inside FD-13."*
*"#5: Enrollment creates a PBKDF2-HMAC-SHA256 admin password verifier with
a per-device random 128-bit salt. Calibrate iterations to 250-500 ms on
ESP32-C6 with a hard minimum of 50,000; store the selected iteration count
with the verifier. Never store or log plaintext password."*

**A new runtime-queryable production/development distinction** was needed
before #2 could be written at all. Recon found `CONFIG_ZGW_PRODUCTION_BUILD`
is a build-time-only CMake environment variable that selects which
`sdkconfig.defaults*` files get layered (`CMakeLists.txt`) -- it never
becomes a compiled `CONFIG_ZGW_*` macro a source file can branch on. A new
`ZGW_PRODUCTION_PROFILE` Kconfig bool closes that gap: `main/Kconfig.projbuild`
declares it (default `n`), `sdkconfig.production.esp32c6` is the **only**
place it is ever set to `y`, and `scripts/verify_production_security_profile.py`'s
`REQUIRED_SYMBOLS` now rejects a production build that omits it -- the same
declare/pin/enforce three-part pattern S5/S6 have used throughout.

**`components/app_hal/include/hal_time.h`/`.c`** (new): `hal_time_now_ms()`,
a monotonic-milliseconds primitive both #3 (window expiry) and #5
(calibration timing) need. Wraps ESP-IDF's real `esp_timer_get_time()`
(`esp_timer/include/esp_timer.h`, confirmed signature against the real
header inside `espressif/idf:release-v5.5` before writing the
declaration; `esp_timer` was already an `app_hal` dependency). Host builds
wrap POSIX `clock_gettime(CLOCK_MONOTONIC, ...)`.

**`components/service/include/admin_verifier.hpp`/`.cpp`** (new, plan #5):
`AdminVerifierRecord` (16-byte salt + 32-byte SHA-256 digest + 4-byte
iteration count, explicitly serialized -- never `reinterpret_cast`,
matching `persisted_device_state.hpp`'s own convention) stored under
`NvsNamespaceId::kAdminVerifier`'s `"admin_verifier"` key
(`encryption_required=true`, Section 2.7 of `PRODUCTION_HARDENING.md`).
`create_admin_verifier()` draws a fresh 128-bit salt from
`hal_random_fill_bytes()`, calibrates the iteration count via
`calibrate_pbkdf2_iterations()` (starts at the 50,000 hard minimum, scales
proportionally against `measure_pbkdf2_duration_ms()`'s real timing,
bounded to 6 attempts, never returns below the hard minimum regardless of
device speed). `verify_admin_password()` re-derives and compares in
constant time (a loop that never short-circuits on the first mismatched
byte), and rejects outright any stored record whose `iterations` field is
below the hard minimum -- a tampered/corrupt record is never trusted to
gate its own verification cost.

**ESP_PLATFORM vs. host crypto backend, an FD-22 "documented unmet product
requirement" carve-out**: production PBKDF2-HMAC-SHA256 uses the real,
non-deprecated `mbedtls_pkcs5_pbkdf2_hmac_ext()` (`mbedtls/pkcs5.h`,
confirmed against the real header inside `espressif/idf:release-v5.5`
before writing the call site -- declared unconditionally, unlike the
PBES2 functions in the same header which are gated behind
`MBEDTLS_ASN1_PARSE_C`/`MBEDTLS_CIPHER_C`). `zgw-host-tools:s0` has **no**
crypto library development headers at all -- confirmed by direct recon
(no `mbedtls*` headers anywhere; OpenSSL *runtime* `.so` files are present
but no `openssl/*.h` headers or `libssl-dev` package, and no Dockerfile
exists in this repository to rebuild that image with them added). Host
builds therefore use a self-contained SHA-256/HMAC-SHA256/PBKDF2-HMAC-SHA256
implementation private to `admin_verifier.cpp`, mirroring `hal_random.c`'s
already-established `ESP_PLATFORM`-vs-host split -- **never** compiled into
a production build. Validated before being trusted, exactly matching the
prior sub-slice's Base32-vs-`base64.b32encode()` discipline: a scratch SHA-256
implementation matched Python's `hashlib.sha256()` byte-for-byte across
empty-string/short/multi-chunk (>55-byte) inputs; HMAC-SHA256 matched
Python's `hmac.new(..., hashlib.sha256)`; PBKDF2-HMAC-SHA256 matched
Python's `hashlib.pbkdf2_hmac('sha256', ...)` for 1-round, 4096-round, and
a second-password vector. (One test-harness mistake was caught by this
same cross-check, not a real algorithm bug: an early scratch HMAC test
hardcoded a message length of 44 instead of the string's real 43 bytes,
reading one byte past the literal -- corrected before any of this logic
reached the real files.)

**`components/service/include/provisioning_secret_provider.hpp`/`.cpp`**
(new, plan #2): a distinct secret from the Wi-Fi AP passphrase Section 2.4
already generates (that one authenticates joining the AP; this one is the
per-device manufacturing proof-of-possession material plan #2 names).
Adapter selection is `CONFIG_ZGW_PRODUCTION_PROFILE`, not
`CONFIG_ZGW_PRODUCTION_BUILD`. Production reads S5's
`manufacturing_provisioning_get_proof_of_possession()`
(`tls_provisioning_storage_port.hpp`) and returns `kUnavailable` (never
falls back to generating one) for any non-`kAvailable` status -- plan #2's
own "production startup fails closed when material is absent." Development
draws 128 bits of fresh randomness every call (never persisted) and logs
it via `ESP_LOGI`, mirroring `main/app_main.cpp`'s existing Wi-Fi AP
passphrase delivery posture (Section 3.3) for the identical reason (no
display, UART only). **Nothing calls `provisioning_secret_provider_get()`
yet** -- the real enrollment/PASE-style consumer is a later S6 sub-slice,
matching `tls_provisioning_storage_port.hpp`'s own established
"port defined ahead of its full pipeline" precedent.

**`components/service/include/commissioning_window.hpp`/`.cpp`** (new,
plan #3): a small, explicit, testable state machine --
`CommissioningWindowState{active, started_at_ms, trigger}` plus
`commissioning_window_start()`/`_is_active()`/`_stop()`, all taking
`now_ms` explicitly rather than reading the clock internally (mirrors
`security_bounds()`'s pure-accessor testability discipline; host tests
control elapsed time deterministically instead of sleeping for real
seconds). Expiry compares against
`security_bounds().commissioning_window_seconds * 1000` (Section 2.1).
`commissioning_window_first_boot_policy_applies()` is plan #3's own
"first-boot policy" trigger, verbatim from the plan's Migration/
compatibility text ("Upgrade boots into restricted migration mode if no
admin credential exists") -- built directly on
`admin_verifier.hpp`'s `admin_credential_exists()`. **Neither the
"trusted physical button action" trigger (no button-driver plumbing exists
in this repository yet) nor any actual gating of commissioning-only
behavior on `commissioning_window_is_active()` is implemented** -- this
sub-slice is the window's own state machine only, the same
"foundation/port before its consumer" pattern as the other two modules in
this section.

**Tests**: `test/host/test_hal_time.cpp` (2 tests: monotonic
non-decreasing across two calls; a sanity bound on the returned value).
`test/host/test_admin_verifier.cpp` (18 tests: not-provisioned before any
write; serialize/deserialize round-trips and rejects wrong length/null
args; `measure_pbkdf2_duration_ms()`/`calibrate_pbkdf2_iterations()` never
go below the hard minimum; `create_admin_verifier()` rejects null/empty
password and null out-pointer, and always uses at least the hard minimum;
create+verify round-trips; wrong password and null/empty password are
rejected; a tampered below-minimum-iterations record is rejected outright;
two verifiers for the same password have different salts; storage is
rejected while encryption is unverified and round-trips once it is;
`admin_credential_exists()` is false before and true after a verifier is
stored). `test/host/test_provisioning_secret_provider.cpp` (3 tests: host
builds always take the development branch -- returns available with the
exact dev byte length, two calls differ, null out is rejected).
`test/host/test_commissioning_window.cpp` (9 tests: active immediately
after start, still active just before expiry, inactive at and after
expiry, inactive before ever being started, defensively inactive if
`now_ms` precedes `started_at_ms`, `stop()` deactivates, restarting
renews the expiry from the new start time, first-boot policy is true
before and false after an admin credential is stored).

**A real bug found only by the real host build, not review**:
`provisioning_secret_provider.cpp`'s `get_production_secret()` (compiled
only under `ESP_PLATFORM && CONFIG_ZGW_PRODUCTION_PROFILE`) tripped
`-Werror=unused-function` on the host build, where it is never referenced
at all. Fixed by moving its definition inside the same `#if` guard as its
one call site rather than leaving it unconditionally defined. A second,
unrelated `-Werror=maybe-uninitialized` in this sub-slice's own new test
file (`test_admin_verifier.cpp`'s wrong-length buffer was declared without
zero-initialization) was caught the same way. This is the **seventh** real
defect this project's S5/S6 work has found only by actually building,
never by review alone.

Full repository host suite: **99/99** passing (95 pre-existing + 4 new
test executables) via `zgw-host-tools:s0`.
`cppcheck --enable=warning,style,performance,portability` via
`zgw-host-tools:cppcheck` reports zero findings against all four new/changed
C/C++ files. `check_arch_invariants.sh` reports `high=0, medium=0, low=0`,
unchanged. Two full real `idf.py build`s inside `espressif/idf:release-v5.5`
succeeded end-to-end -- an ordinary (non-production) build (1194/1194
steps, confirming `admin_verifier.cpp`'s real `mbedtls_pkcs5_pbkdf2_hmac_ext()`
call and `hal_time.c`'s real `esp_timer_get_time()` call both compile and
link) and a `ZGW_PRODUCTION_BUILD=1` build (1197/1197 steps, three more --
`get_production_secret()` itself only exists in this build). The real
generated `sdkconfig` from the production build was independently
re-verified against `verify_production_security_profile.py` (`OK`),
confirming `CONFIG_ZGW_PRODUCTION_PROFILE=y` and
`CONFIG_ZGW_COMMISSIONING_WINDOW_SECONDS=600` both actually appear as
expected in a real generated file, not just in this sub-slice's own source
declarations.

### 2.6 `session_store.hpp`/`.cpp` + `session_security_policy.hpp`/`.cpp` -- bounded session store, cookie, CSRF and CORS policy (plan #13, #14, #15, #16)

This is the first sub-slice in the "HTTPS and sessions" cluster (#7-17).
The cluster was scoped down deliberately, not attempted whole: the user
was asked directly which of three defensible starting points to build
first (`AskUserQuestion`) -- pure session/cookie/CSRF logic (no crypto
library dependency, fully host-testable), the HTTPS listener itself
(needs real `esp_https_server` API recon and has little to gate on before
certificate validation exists), or certificate chain/SAN/CA validation
(needs real mbedtls X.509 parsing, unavailable on the host toolchain the
same way PBKDF2 was, and far too complex to hand-roll a test-only
substitute for, unlike SHA-256/HMAC/PBKDF2 in Section 2.5). The user
chose sessions/cookie/CSRF, confirming the recommended option.

Plan text: *"#13: Add a bounded store of four concurrent sessions. Each
session has a 15-minute idle timeout, 8-hour absolute timeout,
logout/revocation and boot invalidation."* *"#14: Use cookie name
`zgw_session` with `Secure`, `HttpOnly`, `SameSite=Strict` and path
`/api/v1`. CSRF token is a separate 256-bit session-bound value returned
only by the authenticated session endpoint."* *"#15: Require session-bound
CSRF token and same-origin validation for every state-changing browser
request."* *"#16: Reject permissive CORS; default to same-origin only."*

**`components/service/include/session_store.hpp`/`.cpp`** (new, plan #13):
`SessionStoreState` holds a fixed `SessionRecord[4]` array (`kMaxConcurrentSessions`
-- the plan's own exact count), **RAM-only by design**. `session_store_create()`/
`_is_valid()`/`_touch()`/`_revoke()`/`_get_csrf_token()`/`_sweep_expired()` all
take `now_ms` explicitly (a monotonic `hal_time_now_ms()` reading, Section
2.5), mirroring `commissioning_window.hpp`'s own established testability
discipline rather than reading the clock internally. Session IDs (128-bit)
and CSRF tokens (256-bit, plan #14's exact size) are drawn directly from
`provisioning_secrets.hpp`'s `generate_random_secret_hex()` -- the exact
reusable hardware-RNG primitive plan #6 built for this precise later
consumer (its own doc comment already said so). Expiry is fail-closed on
every angle: a record that isn't `in_use` is always "expired"; a
monotonic clock that appears to run backwards is treated as expired
rather than computing an underflowed elapsed value (the same guard
`commissioning_window.cpp` already established). `touch()` extends only
the idle timeout, never the absolute timeout -- proven directly by a test
that touches a session repeatedly, staying inside the idle window every
time, until the absolute timeout still fires anyway.

**A capacity decision made without a plan-text resolution, documented
rather than hidden**: when the store already holds 4 live sessions,
`session_store_create()` returns `kFull` rather than evicting the
oldest/least-recently-active session to make room. Plan #13's "bounded
store of four" is treated as a hard cap, not a sliding window -- an
already-authenticated session is never involuntarily logged out just
because a 5th login occurred, matching the fail-closed, conservative
posture threaded through every S5/S6 sub-slice so far. A caller wanting a
5th concurrent session must log out one first (or wait for an idle/
absolute timeout). This is a real, revisable design choice, not a plan
requirement -- flagged directly (Section 3.1 below and the evidence
file's `frozen_decisions_applied`).

**Deliberate deviation from an earlier session's own speculative design
note**: `nvs_namespace_registry.cpp`'s `kSessionSeed` entry (added during
S5, `implemented_today=false`) describes itself as "session-token seed
material for S6's bounded session store" -- but nothing in plan #13's
literal text requires deriving session tokens from a persisted signing
seed, and RAM-only storage already satisfies "boot invalidation" for
free. This sub-slice generates every session ID and CSRF token from
fresh hardware RNG per session instead, with no persisted seed at all.
`kSessionSeed` remains `implemented_today=false`, unchanged -- flagged as
an open question for the project owner in case a different design (e.g.
stateless HMAC-signed tokens for future multi-listener consistency) was
actually intended.

**`components/service/include/session_security_policy.hpp`/`.cpp`** (new,
plan #14/#15/#16): `build_session_cookie_header()` formats exactly plan
#14's four named attributes --
`"zgw_session=<id>; Secure; HttpOnly; SameSite=Strict; Path=/api/v1"` --
verified against the exact literal expected string, not just a
non-empty-output check. `build_session_cookie_clear_header()` is the
logout-response counterpart, adding `Max-Age=0` -- a *necessary technique*
for a browser to actually delete the cookie (an empty value alone does
not), not an addition to the general cookie's own attribute set, which
stays exactly the plan's four. `session_csrf_token_matches()` (#15's CSRF
half) compares in constant time (never short-circuits on the first
mismatched byte, matching `admin_verifier.cpp`'s password-hash-comparison
discipline from Section 2.5) and rejects a wrong-length token before ever
touching the stored value. `is_same_origin_request()` (#15's same-origin
half) compares a request's raw `Origin` header value against an expected
origin string case-insensitively (ASCII-only, since no case-insensitive
compare convention existed anywhere else in this repository to reuse) and
fails closed on either a missing `Origin` header or an unconfigured
expected origin -- a same-origin browser always sends `Origin` on a
state-changing request, so its absence gets no benefit-of-the-doubt pass.
`cors_cross_origin_allowed()` (#16) is one centralized, grep-able policy
decision (always `false`) rather than left implicit at each future
handler.

**Nothing in this repository calls any function in either file yet.** The
real HTTP request/response plumbing that would read a request's
`Cookie`/`Origin` headers and write a `Set-Cookie` response header belongs
to the production HTTPS listener (plan #7), and the actual
`POST /api/v1/auth/login`/`logout`/`GET /api/v1/auth/session` routes
(plan #17) that would create/revoke sessions -- both separate, not-yet-
implemented S6 sub-slices. This is the session store's and policy's own
logic only, matching this project's established "port/state-machine
defined ahead of its full pipeline" precedent (`tls_provisioning_storage_port.hpp`
in S5, `provisioning_secret_provider.hpp`/`commissioning_window.hpp` in
Section 2.5).

**Tests**: `test/host/test_session_store.cpp` (15 tests): correctly-shaped
fields on create; null-argument rejection; two sessions never share an ID
or CSRF token; valid immediately after create; invalid for an
unknown/null/empty ID; invalid once the idle timeout elapses without a
touch; `touch()` extends the idle timeout but never the absolute timeout
(proven by repeated touching up to the absolute boundary); `touch()`
fails on an unknown or already-expired session; `revoke()` invalidates
and is idempotent; CSRF-token lookup round-trips and rejects
unknown/expired sessions and an undersized output buffer; the store
reports full after exactly `kMaxConcurrentSessions` and rejects a 5th;
revoking one session frees capacity for a new one; an idle-expired slot
is reclaimed by a later create (via the implicit sweep); `sweep_expired()`
clears only the actually-expired slot, leaving a still-valid one intact.
`test/host/test_session_security_policy.cpp` (14 tests): the cookie
header matches the exact literal plan-named string; null/empty-ID and
undersized-buffer rejection for both cookie builders; the clear-cookie
header's exact literal string; CSRF match succeeds for the real
session-bound token and fails for a wrong token, wrong length, or an
unknown/expired session; same-origin passes on an exact and a
mixed-case match, fails for a different origin, and fails closed on a
missing request `Origin` or a missing expected origin; CORS is never
allowed.

Full repository host suite: **101/101** passing (99 pre-existing + 2 new
test executables) via `zgw-host-tools:s0`.
`cppcheck --enable=warning,style,performance,portability` via
`zgw-host-tools:cppcheck` reports zero findings against both new C++
files. `check_arch_invariants.sh` reports `high=0, medium=0, low=0`,
unchanged. A full real `idf.py build` inside `espressif/idf:release-v5.5`
succeeded end-to-end on the first attempt (1196/1196 steps) -- this
sub-slice calls no new ESP-IDF/mbedtls API of its own (only
`hal_time_now_ms()` and `generate_random_secret_hex()`, both already
verified against the real target in prior sub-slices), so this build
mainly confirms the two new translation units compile and link cleanly
under the real target toolchain.

### 2.7 `components/web_ui/web_server.cpp` -- production HTTPS listener, development HTTP fallback (plan #7)

Plan text: *"Replace production `httpd_start` with HTTPS server
configuration. Development HTTP is gated by an explicit non-production
profile."*

**Real API recon before writing any code, following this project's
standing discipline**: `espressif/idf:release-v5.5`'s actual
`esp_https_server` component was grepped directly -- `httpd_ssl_config_t`
(an `httpd_config_t` base plus `servercert`/`servercert_len`,
`prvtkey_pem`/`prvtkey_len`, `transport_mode`, `port_secure`/
`port_insecure`), `HTTPD_SSL_CONFIG_DEFAULT()`, and the real
`httpd_ssl_start()`/`httpd_ssl_stop()` signatures and implementations (the
latter read in full: `httpd_ssl_stop()` is a thin wrapper around plain
`httpd_stop()` plus an event post -- calling the paired stop function
still matters for the event, even though the transport-context cleanup
callback `httpd_ssl_start()` registers would fire correctly either way).
**A real, load-bearing surprise found by this recon**: `esp_https_server`'s
own `Kconfig` declares `CONFIG_ESP_HTTPS_SERVER_ENABLE`, but grepping the
component's actual `.c`/`.h`/`CMakeLists.txt` found this symbol is **never
referenced anywhere** -- `httpd_ssl_start`/`_stop` compile unconditionally
once `esp_https_server` is a component `REQUIRES` dependency, regardless
of that Kconfig bool's value. It is set to `y` in
`sdkconfig.defaults.esp32c6` anyway, for menuconfig-visible documentation
of intent and in case a future ESP-IDF version does start gating on it --
but the actual compilation gate this sub-slice relies on is
`CONFIG_ZGW_PRODUCTION_PROFILE`, not this symbol.

**`components/web_ui/web_server.cpp`** (changed): `WebServer::start()` now
branches on `CONFIG_ZGW_PRODUCTION_PROFILE` (the same S6-slice-3 symbol
already used by `provisioning_secret_provider.cpp`'s adapter switch, never
`CONFIG_ZGW_PRODUCTION_BUILD`):
- **Production**: reads the S5 "current" certificate and private-key slot
  (`tls_provisioning_storage_port.hpp`'s `tls_identity_get_certificate()`/
  `tls_identity_get_private_key()`, plan S5 #13) and starts a real
  `httpd_ssl_start()` listener with `transport_mode =
  HTTPD_SSL_TRANSPORT_SECURE`. If either read is not
  `SecureStorageStatus::kAvailable`, the listener refuses to start at all
  -- fails closed, **never** falls back to plain HTTP -- matching the
  plan's own closing text ("the production listener... remain[s] disabled
  until certificate trust... [is] healthy").
- **Development** (`CONFIG_ZGW_PRODUCTION_PROFILE` unset): the previously
  unconditional plain-HTTP `httpd_start()` path, now explicitly gated
  behind "not production" instead of being the only code path --
  satisfies the plan's "Development HTTP is gated by an explicit
  non-production profile" text literally. `WebServer::stop()` now tracks
  which start function created the handle (`using_https_`) and calls the
  matching stop function.

**An honest, named scope boundary, not a silent gap**: this sub-slice
does **not** implement certificate chain-to-CA, SAN (mDNS host +
`urn:zgw:<gateway_id>` URI) or issuer/expiry validation (plan #10-#11) --
it only checks that certificate/key material is *present* in storage, not
that it is *trustworthy*. Since no real certificate-issuance/rotation
workflow exists yet (plan #10-#12, S5's own storage interfaces
deliberately "generate... no real certificate, private key or
provisioning secret"), **a production build's HTTPS listener will always
refuse to start today** -- every real production build currently ends up
in the fail-closed branch, by design, until a future sub-slice actually
populates the "current" slot. This is the expected, documented consequence
of scoping #7 alone out of the "HTTPS and sessions" cluster, not a defect.

**A significant, real consequence worth stating plainly, not just
implying**: `main/app_main.cpp`'s existing call site (unchanged by this
sub-slice) treats `WebServer::start()` failure as fatal -- an infinite
`while(true) { vTaskDelay(...); }` halt, the same pre-existing
fail-closed convention already used for Wi-Fi AP and mDNS start failures.
This was not a new failure mode this sub-slice introduced, but its
trigger condition (no certificate material present) is now *always* true
for a `CONFIG_ZGW_PRODUCTION_PROFILE=y` build until #10-#12 exist -- so a
real production-profile binary flashed today would halt at boot,
indefinitely, with only a log line as evidence. The plan's own text is
not fully unambiguous here ("runtime failure leaves the routes
unregistered" reads more like "web UI absent, rest of the device still
functions" than "halt the whole device") -- this sub-slice deliberately
did not change `app_main.cpp`'s existing all-or-nothing halt semantics
(out of scope for #7 alone), so this tension is inherited from the
existing call site, not introduced fresh. Not an urgent operational risk
today -- this project has no live deployment or real users yet (a
production-profile binary is not actually being flashed onto real
hardware anywhere) -- but worth the project owner's explicit attention
before that changes.

**Data-format contract flagged for the future writer of the cert/key
slot**: `mbedtls_x509_crt_parse()`'s own PEM convention requires the
buffer length to include the trailing NUL terminator. This file passes
the stored blob length through to `servercert_len`/`prvtkey_len` exactly
as read, without inserting or verifying a NUL itself -- whichever future
#10-#12 sub-slice writes real certificate/key bytes via
`tls_identity_set_certificate()`/`_set_private_key()` must store
NUL-terminated PEM text for this listener to actually parse it correctly.

**No host test exists for this file, matching its own established
convention, not a new gap this sub-slice introduces**: `web_server.cpp`
requires `esp_http_server.h`/`esp_https_server.h`, both `ESP_PLATFORM`-only
with no host equivalent (same as `hal_wifi.c`'s AP setup and
`hal_ota.c`'s manifest-signature verification) -- it was never part of
`test/host/CMakeLists.txt` before this change either. Verification is via
two full real `idf.py build`s only: this project's Docker toolchain has no
QEMU/hardware to actually *run* the resulting firmware and observe a
successful TLS handshake, so -- exactly like every other `ESP_PLATFORM`-only
HAL/composition-root code in this project -- "compiles and links against
the real target API" is the established, accepted verification depth
here, not a shortcut unique to this sub-slice.

**A repeat of a previous sub-slice's own known failure mode, caught
proactively this time**: `S6-provisioning-credentials-completion.json`
found that a function compiled only under
`CONFIG_ZGW_PRODUCTION_PROFILE` but defined unconditionally trips
`-Werror=unused-function` on whichever build doesn't reach it. Both
`start_production_https()` and `start_development_http()` were written
inside their own matching `#if`/`#else` guard from the start this time,
specifically to avoid repeating that exact bug -- and indeed neither real
`idf.py build` (ordinary or `ZGW_PRODUCTION_BUILD=1`) found a new defect
this time.

**Tests**: none new -- this file has no host-testable surface (see
above). Verified via two full real `idf.py build`s inside
`espressif/idf:release-v5.5`: an ordinary (non-production) build,
1196/1196 steps, confirming the development `httpd_start()` path and the
`start_production_https()`/`build_base_httpd_config()` functions compile
correctly guarded out; and a `ZGW_PRODUCTION_BUILD=1` build, 1199/1199
steps (three more -- `start_production_https()`, its
`tls_identity_get_certificate()`/`_get_private_key()` calls, and the real
`httpd_ssl_start()` call itself only exist in this build). `cppcheck
--enable=warning,style,performance,portability` via
`zgw-host-tools:cppcheck` reports zero findings (analyzing only the
`ESP_PLATFORM`-independent boilerplate it can see without the real
ESP-IDF headers). `check_arch_invariants.sh` reports `high=0, medium=0,
low=0`, unchanged. The full 101-test host `ctest` suite was re-run to
confirm zero regressions from the `web_ui`/`CMakeLists.txt` change (this
sub-slice touches no host-tested source file) -- still 101/101.

**Real hardware-in-the-loop update (2026-08-14, after real ESP32-C6
hardware became available this session)**: `start_production_https()`
was actually *run* on real hardware for the first time (Flash Encryption
enabled in Development mode + `CONFIG_ZGW_PRODUCTION_PROFILE=y`, a
temporary, session-scoped HIL configuration -- see
`implementation-evidence/HIL-flash-encryption-and-https-listener-verification.json`).
It crashed immediately with a real stack overflow: `cert_bytes`/
`key_bytes`/`ca_bytes` (three 4096-byte buffers, 12KB total) as ordinary
stack locals against the `main` task's ~4KB budget -- compile-and-link
verification (the two builds referenced above) cannot detect runtime
stack usage, so this was invisible until the code actually executed.
Fixed by moving all three buffers to `static` storage (safe: this
function has exactly one call site, invoked exactly once at boot) --
this also closes a latent correctness risk independent of the stack
overflow, since `config.servercert`/`config.prvtkey_pem` are raw
pointers into these buffers and `esp_https_server`'s own internal
copy-vs-retain behavior for them was never confirmed via recon. After
the fix, a real boot confirmed the full intended chain works end-to-end
for the first time: `secure_storage: read key='tls_cert_cur' status=1`
(not provisioned) -> `Production HTTPS listener: current certificate
unavailable -- refusing to start (fail closed)` -> a clean halt via
`app_main.cpp`'s existing critical-failure handling, zero panics, zero
reboots. See the HIL evidence file above for the full incident
(including a real, fully-recovered flash-corruption mistake made and
fixed mid-session -- reflashing an already-flash-encrypted device
requires `idf.py encrypted-flash`, never a hand-copied `esptool.py
write_flash` command missing `--encrypt`).

**Real hardware-in-the-loop update (2026-08-14, third HIL round -- positive
path confirmed):** the fail-closed path above was only half the story. A
real private CA + device leaf certificate (EC P-256, correct DNS SAN
`zigbee-gateway-9f0020.local` and URI SAN `urn:zgw:98a3169f0020`, matching
this device's real, corrected `GatewayId` -- see Section 2.8's own HIL
update) was generated and loaded into this device's encrypted NVS via a
temporary provisioning hook exercising `service::tls_identity_set_*()`
(S5 plan #13's interface, never previously called by anything). The
production HTTPS listener started for real: `esp_https_server: Server
listening on port 443`, zero fail-closed log lines. After removing the
temporary hook and reflashing clean firmware (which never touches the
`nvs` partition), the same listener started again purely from persisted
encrypted state, confirming the real, shipped code path -- not test
scaffolding -- is what works. Also confirmed by inspecting the real
generated `sdkconfig`: `CONFIG_MBEDTLS_HAVE_TIME_DATE` is unset in this
project's build, so mbedtls's X.509 expiry checking is compiled out
entirely (not merely untested against a correct clock) -- this project
has no SNTP/RTC wall-clock source anywhere, a named gap for whenever
expiry enforcement is revisited. An external TLS handshake against the
listener was not attempted (no network path from this tool environment
onto the device's own Wi-Fi AP). Full evidence:
`implementation-evidence/HIL-real-tls-certificate-end-to-end-verification.json`.

### 2.8 Production mDNS host derivation (plan #9)

Plan text: *"Derive production mDNS host exactly as
`zigbee-gateway-<last6>.local` and advertise only `https://`."*

**`components/service/provisioning_secrets.cpp`** (changed): the private
`build_prefixed_gateway_suffix_name()` helper (the exact "<prefix>-<last6-hex>"
logic `build_provisioning_ap_ssid()`, plan #4, already implemented) is now
shared by a new `build_gateway_mdns_host()`:
- **Production** (`CONFIG_ZGW_PRODUCTION_PROFILE=y`): exactly
  `"<kGatewayHostNamePrefix>-<last6-hex>"`, i.e. plan #9's own literal
  text (`kGatewayHostNamePrefix = "zigbee-gateway"`, a new shared
  constant replacing what used to be a locally-duplicated string in
  `main/app_main.cpp`).
- **Development** (`CONFIG_ZGW_PRODUCTION_PROFILE` unset, or a host
  build): the plain, unsuffixed `kGatewayHostNamePrefix` -- a fixed,
  predictable hostname is more convenient for bench-testing a single
  device, and per-device uniqueness is a convenience concern here, not a
  trust concern the way it is in production (development already serves
  plain HTTP unconditionally, Section 2.7's own plan #7 branch).

Same branching convention as every other S6 dev/prod switch this stage
has built: `CONFIG_ZGW_PRODUCTION_PROFILE`, never `CONFIG_ZGW_PRODUCTION_BUILD`.
Unlike Section 2.7's `web_server.cpp` (which branches at the composition
root, matching the plan's own "only production listener/control-plane
composition root" framing for that file specifically), this branch lives
inside the service-layer function itself -- matching
`provisioning_secret_provider.cpp`'s own precedent (a single function
name, internal dev/prod branching) since mDNS host naming is a naming
decision, not a listener-composition decision.

**`main/app_main.cpp`** (changed): the local `kGatewayHostName` constant
is gone, replaced by `service::kGatewayHostNamePrefix` (single source of
truth, shared with the AP SSID call site so the two can never drift
independently) and a call to `build_gateway_mdns_host()`. The logged URL
scheme now matches what the build variant actually serves --
`https://%s.local` under `CONFIG_ZGW_PRODUCTION_PROFILE`, `http://%s.local`
otherwise -- rather than the prior code's unconditional `http://`, which
would have been actively wrong advice for a production build now serving
real HTTPS (Section 2.7). Fails closed (halts, matching every other
critical-service-start failure in this file) if hostname generation
itself fails.

**Tests**: 4 new tests appended to `test/host/test_provisioning_secrets.cpp`
(host builds always exercise the development branch, matching
`provisioning_secret_provider.cpp`'s own established host-testing
convention for its production-gated logic): the plain prefix is returned
on a host build; an invalid `GatewayId` is tolerated (the development
branch never reads it at all, unlike `build_provisioning_ap_ssid()`'s own
production-relevant validity check); an undersized buffer and a null
output pointer are both rejected.

Full repository host suite: **101/101** passing (no new test executable
this time -- tests were appended to the existing `test_provisioning_secrets`
binary). `cppcheck --enable=warning,style,performance,portability` via
`zgw-host-tools:cppcheck` reports zero findings against the changed file.
`check_arch_invariants.sh` reports `high=0, medium=0, low=0`, unchanged.
Two full real `idf.py build`s inside `espressif/idf:release-v5.5`
succeeded end-to-end on the first attempt -- an ordinary build (1196/1196
steps, confirming the development branch) and a `ZGW_PRODUCTION_BUILD=1`
build (1199/1199 steps, confirming the production `"<prefix>-<last6>"`
branch actually compiles under the real target's `CONFIG_ZGW_PRODUCTION_PROFILE=y`).

**Real hardware-in-the-loop update (2026-08-14, second HIL round):** the
suffix this section describes (`<last6-hex>` of the `GatewayId`) is only
as correct as the `GatewayId` it's fed. Real boot logs on the connected
ESP32-C6 consistently showed `zigbee-gateway-fffe9f` instead of the
expected `zigbee-gateway-9f0020` (derived from this device's real factory
base MAC `98:a3:16:9f:00:20`). Root cause: `hal_identity_get_factory_base_mac()`
(`components/app_hal/hal_identity.c`) called `esp_efuse_mac_get_default()`
with only a 6-byte buffer, but that ESP-IDF function is documented to
require 8 bytes on any `SOC_IEEE802154_SUPPORTED` target (ESP32-C6,
ESP32-H2) -- it writes a 6-byte MAC and then expands it in place to an
8-byte IEEE 802.15.4 EUI-64 form, which for a 6-byte destination is a
2-byte out-of-bounds stack write on every single boot, and also silently
corrupts the value the caller reads back (the real trailing MAC bytes
`9f:00:20` are replaced with the constant EUI-64 padding `ff:fe` plus
one surviving byte, `9f`). Fixed by switching to
`esp_read_mac(out, ESP_MAC_EFUSE_FACTORY)`, which reads the identical
eFuse field without ever performing the EUI-64 expansion. Confirmed
fixed on real hardware: post-fix boot logs show
`SSID=zigbee-gateway-9f0020` and `mDNS started: https://zigbee-gateway-9f0020.local`,
matching the real base MAC. This also affects Section 2.10's URI SAN
(`urn:zgw:<gateway_id>`), which was corrupted the same way before this
fix. Full evidence: `implementation-evidence/HIL-gateway-id-factory-mac-corruption-fix.json`.

### 2.9 Gateway identity self-consistency verification (plan #8's remaining half)

Plan text: *"Read `GatewayId` only from the ESP32 factory base MAC and
render exactly 12 lowercase hex characters. Verify manufacturing/provisioning
uniqueness; duplicate/cloned GatewayId evidence blocks production
enrollment."* GatewayId derivation itself (the first sentence) already
existed from S4; this sub-slice is the second sentence only. FD-17,
verbatim: *"Manufacturing/provisioning evidence must reject duplicate or
cloned GatewayId enrollment."*

**An honest scoping decision, made explicit rather than force-fit**: the
plan's own "uniqueness" language most naturally means detecting a
GatewayId reused across two *different physical devices* -- fleet-wide
duplicate-enrollment detection. That needs infrastructure this project
does not have at all (this repository is a single-gateway firmware
codebase, not a fleet-management backend) and, in any case, depends on
the real eFuse burn/attestation workflow (plan #6-#8, S5) that
`S5-completion.json` already found `BLOCKED_SECURITY_PROVISIONING` (no
manufacturing/eFuse environment exists in this sandbox). Building a
"uniqueness check" with no real manufacturing backend or attestation to
check against would produce something that verifies nothing real --
worse than not building it.

**What this sub-slice builds instead**: a narrower, still-meaningful,
fully buildable **local self-consistency check** -- does the GatewayId
this device's firmware reads *live* from its own factory base MAC match
the GatewayId that was recorded *once*, during manufacturing, in
encrypted storage? A mismatch is real, concrete evidence of exactly the
failure mode this plan's own FD-17 text names elsewhere (the HA-discovery
footnote, plan line 1299: *"cloning the firmware image to different
hardware therefore yields a different gateway identity"*) -- this
device's current hardware does not match what was recorded when it was
manufactured. This is explicitly **not** the same guarantee as fleet-wide
duplicate-enrollment detection (a single device cannot observe any other
device at all) -- the module's own header comment says so directly, not
overclaimed.

**`components/service/include/gateway_identity_verification.hpp`/`.cpp`**
(new): `get_stored_manufacturing_gateway_id()`/`set_stored_manufacturing_gateway_id()`
are typed accessors over `secure_storage_get_blob()`/`set_blob()` (S5
Section 2.8/2.9), scoped to a **new** `"mfg_gateway_id"` key in
`NvsNamespaceId::kManufacturingProvisioning` (extended from 2 to 3 key
patterns) -- the raw 6-byte `GatewayId`, deliberately **not** the full
eFuse-provisioning-template JSON record
(`manufacturing_provisioning_get_efuse_record()`, plan S5 #5,
`tls_provisioning_storage_port.hpp`). Parsing that JSON on-device today
would duplicate plan #24's not-yet-built `cJSON`-backed
`StrictJsonObjectReader`, which this project's own FD text forbids
("Application-owned code may add ports/adapters... it must not duplicate
a platform state machine or cryptographic primitive without a documented
unmet product requirement") -- a JSON parser is exactly the kind of thing
#24 is meant to consolidate into one place, not something this narrower
sub-slice should improvise its own copy of.

`verify_gateway_id_against_manufacturing_record(live_gateway_id)` returns
`GatewayIdVerificationResult{kVerified, kMismatch, kNoManufacturingRecord}`.
**Fails closed on every non-match outcome**: `kNoManufacturingRecord` is
treated exactly the same as `kMismatch` by
`gateway_id_verification_allows_production_enrollment()` -- absence of
manufacturing evidence is never implicit proof of authenticity. Since no
real manufacturing workflow populates this record yet
(`BLOCKED_SECURITY_PROVISIONING`, same reason as above), **this predicate
always returns `false` in a real deployment today** -- an expected,
documented consequence, not a defect, mirroring
`provisioning_secret_provider.hpp`'s own production adapter (Section
2.5), which also always fails closed today for the identical
missing-manufacturing-material reason.

**Nothing in this repository calls any function in this module yet** --
the real consumer is plan #17's `POST /api/v1/provisioning/enroll` route,
a separate, not-yet-implemented S6 sub-slice. Matches this project's
established "port/state-machine defined ahead of its full pipeline"
precedent (`tls_provisioning_storage_port.hpp` in S5,
`provisioning_secret_provider.hpp`/`commissioning_window.hpp` in Section
2.5, `session_store.hpp`/`session_security_policy.hpp` in Section 2.6).

**Tests**: `test/host/test_gateway_identity_verification.cpp` (8 tests):
not-provisioned before any write; `kNoManufacturingRecord` before any
write, and confirmed to never allow production enrollment; a null output
pointer is rejected; a write is rejected while encryption is unverified
and round-trips once it is; verification reports `kVerified` (and allows
enrollment) when the live and recorded `GatewayId` match; verification
reports `kMismatch` (and never allows enrollment) when they differ.

Full repository host suite: **102/102** passing (101 pre-existing + 1 new
test executable) via `zgw-host-tools:s0`. `cppcheck
--enable=warning,style,performance,portability` via
`zgw-host-tools:cppcheck` reports zero findings.
`check_arch_invariants.sh` reports `high=0, medium=0, low=0`, unchanged.
A single full real `idf.py build` inside `espressif/idf:release-v5.5`
succeeded end-to-end on the first attempt (1197/1197 steps) -- only one
build was needed this time (unlike Sections 2.5/2.7/2.8) since this
module has no `CONFIG_ZGW_PRODUCTION_PROFILE` branch of its own to verify
separately; it is a plain, unconditionally-compiled service-layer module.

### 2.10 TLS certificate chain/SAN/expiry/key validation (plan #10, #11)

Plan text: *"#10: Production uses the S5 encrypted `current` certificate/key
slot. Its certificate must chain to the configured product management CA
and contain both the exact mDNS DNS SAN and URI SAN
`urn:zgw:<gateway_id>`. Development profile may generate a visibly
development-only self-signed certificate. Production never generates or
accepts an untracked self-signed certificate."* *"#11: Product CA trust
is distributed to admin/browser clients out of band. Missing CA, invalid
issuer/SAN/expiry/key match or unreadable current slot keeps the
production management listener disabled."*

**Real mbedtls API recon before writing any code, following this
project's standing discipline** -- and this time reading the library's
own *implementation*, not just its header comments: `mbedtls_x509_crt_verify()`
performs chain-to-CA validation, expiry/not-yet-valid checking
(`MBEDTLS_X509_BADCERT_EXPIRED`/`_FUTURE`), and a Subject Alternative
Name match against its `cn` parameter, **all in one call** -- confirmed
by reading `x509_crt_verify_name()`/`x509_crt_check_san()` inside the
real `x509_crt.c` source (not assumed from the header's doc comment
alone, which only implied this). Since exactly two independent SAN
values (a DNS name and a URI) each need to be present, and `cn` checks
only one identity value per call, this module calls
`mbedtls_x509_crt_verify()` **twice** against the same parsed chain
rather than hand-rolling its own SAN-list walk -- trusting mbedtls's own
exact-match logic is the safer choice for security-sensitive comparison
code than re-implementing it. Private-key/certificate pairing uses
`mbedtls_pk_check_pair()`. Blinding randomness comes from ESP-IDF's own
`mbedtls_esp_random()` (`mbedtls/esp_mbedtls_random.h`) -- a real,
public, documented "suitable for passing as f_rng" wrapper found during
recon, not assumed to exist.

**A real architecture-gate finding, corrected before it ever reached a
build**: the first version of this validation logic was written under
`components/service` (matching where every other S6 sub-slice's logic
lives). `check_arch_invariants.sh`'s `INV-H002` ("service layer must not
use malloc/calloc/realloc/free") immediately flagged
`mbedtls_x509_crt_free()`/`mbedtls_pk_free()` -- and correctly so: these
are mbedtls's own RAII-style cleanup for its internally heap-allocated
parse buffers, exactly the same category of call `hal_ota.c`'s own
pre-existing `mbedtls_pk_free()`/`mbedtls_ecdsa_free()` calls already
make. `components/app_hal` -- not `components/service` -- is where this
project's architecture already puts that kind of code, and `INV-H002`'s
check does not scope to `components/app_hal` at all. Relocated to
**`components/app_hal/include/hal_tls_certificate_validator.h`/`.c`**
(a plain-C `hal_*` module, matching every other file in that directory)
instead of suppressed via `docs/architecture/ADR_EXCEPTIONS.md` -- that
mechanism is for *temporary* exceptions with an expiry date, not a
permanent architectural fit. This is exactly the kind of real,
tool-caught correction this project's discipline exists to produce --
found by the static gate this time, not a build failure.

**`hal_tls_validate_certificate()`** validates an already-read
certificate/private-key/CA byte triple -- it does not itself read from
storage (`web_server.cpp`'s own `start_production_https()`, Section 2.7,
already gates on `SecureStorageStatus::kAvailable` for all three,
satisfying #11's "unreadable current slot" clause on its own). Returns
`HAL_TLS_CERT_VALIDATION_VALID` only if the certificate parses, chains to
the CA, is within its validity period, contains both SAN values exactly,
and the private key matches the certificate's public key -- otherwise one
of six specific failure codes, logged (never logging certificate/key
*content*, only the numeric mbedtls verification flags for diagnosis).
ESP_PLATFORM-only, with a fail-closed host stub (`HAL_TLS_CERT_VALIDATION_CERTIFICATE_PARSE_FAILED`
unconditionally) matching `hal_ota.c`'s own established `#ifdef ESP_PLATFORM`-guarded-body
pattern exactly -- host builds have zero crypto library dev headers
available at all (the same blocker `admin_verifier.cpp`'s PBKDF2 work
found), and unlike SHA-256/HMAC/PBKDF2, hand-rolling a test-only X.509
parser/verifier would be far too complex and risky to trust.

**`provisioning_secrets.hpp`/`.cpp`** gained `build_gateway_uri_san()`
(builds exactly `"urn:zgw:<12-lowercase-hex-chars>"`, plan #10's own
literal text) -- pure string formatting with no crypto dependency, kept
host-testable exactly like this file's other two builders even though
its one real consumer is ESP_PLATFORM-only.

**`components/web_ui/web_server.cpp`** (changed): `start_production_https()`
now also reads the product CA (`tls_identity_get_product_ca()`, S5,
previously read by nothing), builds the expected DNS SAN
(`build_gateway_mdns_host()` + `".local"` -- that function alone does not
include the suffix, `hal_mdns.h`'s own `mdns_hostname_set()` appends it
separately) and URI SAN (`build_gateway_uri_san()`), and calls
`hal_tls_validate_certificate()` before ever calling `httpd_ssl_start()`.
Any non-`kValid` result keeps the listener from starting -- fails closed,
matching #11's text exactly. Since no real certificate-issuance workflow
exists yet (plan #10-#12's own remaining piece, Section 2.7's own
already-documented consequence), **a production build's HTTPS listener
still always refuses to start today** -- this sub-slice makes the
fail-closed gate real and complete, it does not (and cannot yet) make the
listener actually start.

**A second real bug, found only by the real `ZGW_PRODUCTION_BUILD=1`
target build, not the ordinary build or cppcheck**: the new
`runtime_->gateway_id()` call in `WebServer::start()`'s production branch
failed with `error: invalid use of incomplete type 'class
service::ServiceRuntimeApi'` -- `web_server.hpp` (via `web_routes.hpp`)
only forward-declares that class, and no translation unit previously
needed the full definition. The *ordinary* (non-production) build never
exercises this code path at all (it is compiled out entirely, matching
Section 2.7's own established `#if CONFIG_ZGW_PRODUCTION_PROFILE` guard
convention), so only the production-profile build could have caught this
-- and did. Fixed by adding `#include "service_runtime_api.hpp"`;
confirmed via a second, clean production build. This is the **ninth**
real defect this project's S5/S6 work has found only by actually
building, never by review alone -- and the second inside this single
sub-slice, alongside the architecture-gate finding above.

**Tests**: `test/host/test_hal_tls_certificate_validator.cpp` (2 tests):
the host stub never reports a certificate valid, for both plausible
dummy input and null/zero-length input -- the full extent of what a host
build can verify for this ESP_PLATFORM-only module. 4 new tests appended
to `test/host/test_provisioning_secrets.cpp` for `build_gateway_uri_san()`:
the exact plan-named format; invalid-`GatewayId` rejection; undersized-buffer
rejection; null-out rejection.

Full repository host suite: **103/103** passing (101 pre-existing + 2 new
test executables) via `zgw-host-tools:s0`. `cppcheck
--enable=warning,style,performance,portability` via
`zgw-host-tools:cppcheck` reports zero findings against all three
changed/new files. `check_arch_invariants.sh` reports `high=0, medium=0,
low=0` -- confirming the `INV-H002` violation found mid-development was
fully resolved by relocation, not suppressed. Two full real `idf.py build`s
inside `espressif/idf:release-v5.5` succeeded end-to-end -- an ordinary
build (1198/1198 steps, confirming the real mbedtls X.509/PK calls
compile even though nothing calls `hal_tls_validate_certificate()` at
runtime in this configuration) and, after fixing the incomplete-type bug
above, a `ZGW_PRODUCTION_BUILD=1` build (1201/1201 steps, confirming the
actual production call site -- CA read, SAN construction, and the
validation call itself -- compiles and links).

**Real hardware-in-the-loop update (2026-08-14/15):** a real private CA +
device certificate satisfying this section's own chain/SAN/expiry/key
checks was generated and loaded onto the connected ESP32-C6, and the
production HTTPS listener was confirmed to actually start
(`esp_https_server: Server listening on port 443`) -- the first real
positive-path confirmation of this whole #7/#10/#11 chain, not just
compile verification. See `implementation-evidence/HIL-real-tls-
certificate-end-to-end-verification.json` and Section 2.7/2.8's own HIL
addenda. The "no real certificate exists anywhere" framing in Section
3 below predates this and is now stale for the specific *test/HIL*
device it was loaded onto -- it remains true for any device that has not
been through that same manual, one-off provisioning exercise, since no
real certificate-issuance/loading pipeline exists yet (Section 2.11 below
does not add one either).

### 2.11 Central authorization middleware, capability taxonomy and login/logout/session routes (plan #18, #19, #23, and the login/logout/session subset of #17)

Plan text: *"Define capabilities: read_status; control_device;
manage_network; commission_device; remove_device; firmware_admin;
rcp_admin; security_admin; factory_reset."* (#18) *"Central middleware
authenticates and authorizes before request-body parsing/use-case
invocation."* (#19) *"Direct route access without capability returns
non-leaky stable errors and does not reveal private state."* (#23)

**Scope note, decided with the user before writing any code**: #19's
middleware has nothing to authenticate a caller against without a login
route to create a session in the first place -- so this sub-slice's scope
was explicitly widened to include #17's `POST /api/v1/auth/login`,
`POST /api/v1/auth/logout` and `GET /api/v1/auth/session`, even though
#17 is nominally filed under the "HTTPS and sessions" cluster. The other
four #17 routes (`provisioning/enroll`, `auth/password`, certificate
rotation, factory-reset) all need the physical-presence grant (#20-#22,
not built -- needs a GPIO/button HAL this repository does not have) and
stay unregistered.

**New `components/service/include/capability.hpp`/`.cpp`**: the plan's
exact 9-value `Capability` enum and `capability_token()`. This system has
exactly one administrative role (a single `AdminVerifierRecord`, Section
2.5) -- there is no user/role table to check a real per-capability subset
against, so `granted_capabilities()` grants every capability the current
build actually supports (withholding `firmware_admin`/`rcp_admin` when
OTA/RCP are not built) rather than implementing a fake check with nothing
real to verify. Documented as a deliberate, reversible scope boundary, the
same pattern `gateway_identity_verification.hpp`'s own self-consistency
(not fleet-uniqueness) scoping already established.

**New `components/service/include/route_authorization.hpp`/`.cpp`**:
`authorize_read_request()`/`authorize_mutation_request()` -- pure,
host-testable decision functions built entirely on Section 2.6's
`session_store.hpp`/`session_security_policy.hpp` primitives. A read
needs only a valid session; a mutation additionally needs a matching
CSRF token and same-origin `Origin` header (plan #15). Neither function
branches on a `Capability` value -- see `capability.hpp`'s own comment for
why that is honest rather than an oversight.

**New `components/web_ui/include/web_route_auth_dispatch.hpp`/`.cpp`**:
the actual "central middleware" plan #19 names, implemented as a
registration-level dispatch wrapper (`register_authenticated_uri_handler_v1()`
+ a shared `authenticated_dispatch_trampoline`), not as a check inlined
into each of the 13 pre-existing v1 handler function bodies. Every
pre-existing v1 handler's own host test calls that handler function
directly, never through a real dispatch path -- an inline check would
have forced every one of those tests to first fabricate a valid session,
conflating business-logic testing with authentication testing. The
wrapper stores a small, fixed-capacity (`kMaxAuthenticatedRoutes = 24`,
no malloc/new) table of `{real handler, WebRouteContext*, Capability}`
bindings; at real request time the trampoline calls
`web_v1_common.hpp`'s new `authorize_v1_request()` (the HTTP-layer glue --
reads the `zgw_session` cookie via the real `httpd_req_get_cookie_val()`,
and for a state-changing request the `X-CSRF-Token`/`Origin` headers via
`httpd_req_get_hdr_value_str()`) before ever calling through to the real
handler, restoring `req->user_ctx` to the real `WebRouteContext*` first so
the real handler's own body is completely unaware the wrapper exists.

**New `components/web_ui/include/web_v1_common.hpp`/`.cpp` additions**:
two new `ApiV1ErrorCode` values in the existing golden-matrix convention
-- `kUnauthenticated` (401) and `kCsrfOrOriginInvalid` (403), satisfying
plan #23's "non-leaky stable errors" the same way every other v1 error
already does (a terse `{"schema_version":1,"error":"<token>"}` body, never
echoing which specific check failed beyond that one classification).

**New `components/web_ui/web_handlers_auth.cpp`**: `auth_login_post_handler_v1()`
(reads `{"password":"..."}`, fails closed via `kCapabilityUnavailable` if
no admin credential has ever been enrolled -- no enrollment flow exists
yet, so this is the real state of a fresh device, not a defensive-only
branch; verifies via Section 2.5's `verify_admin_password()`; on success
creates a session via `session_store_create()` and sets the exact plan
#14 `Set-Cookie` header; never distinguishes wrong-password from
no-such-account since there is only ever one account), `auth_logout_post_handler_v1()`
(revokes the session, sends the clear-cookie header -- registered as a
mutation-grade wrapped route, requiring CSRF+origin like any other
state-changing request, since the plan names no logout exception),
`auth_session_get_handler_v1()` (`GET /api/v1/auth/session`, wrapped
read-grade: returns the session's CSRF token and `granted_capabilities()`
as a JSON array -- plan #14's own text assigns CSRF-token delivery to
exactly this route, so login's own response never includes it).

**`components/web_ui/web_handlers_v1.cpp`**: all 13 pre-existing v1 route
registrations now go through `register_authenticated_uri_handler_v1()`
with a named `Capability` (e.g. device power -> `kControlDevice`, OTA
operations -> `kFirmwareAdmin`, config `PATCH` -> `kManageNetwork`)
instead of a raw `httpd_register_uri_handler()` call -- a mechanical,
one-line-per-route change; no handler function body was touched.
`register_web_routes_v1()` now also registers the three auth routes
first and requires `context->sessions`/`context->expected_origin` to be
set.

**`components/web_ui/web_server.cpp`**: `WebServer` now owns a
`SessionStoreState` member and a fixed `expected_origin_` buffer, wired
into `route_context_` in the constructor. `start()`'s production branch
builds `expected_origin_` as `"https://" + <production mDNS host> +
".local"` (Section 2.8's own derivation) and calls `register_web_routes_v1()`
-- **the first-ever production registration of the `/api/v1` contract**,
previously forbidden by plan S4 required changes #28/#29 ("S6 owns the
first production registration path") and enforced by `INV-H010`.
Re-checked before writing any code: `INV-H010`'s `check_absent` guard
name-greps `main/app_main.cpp` specifically, never
`components/web_ui/web_server.cpp` (the real composition root) -- the
invariant was always scoped to block the wrong file from doing this, not
to block it everywhere. No invariant-script change was made or needed;
re-verified via a real `check_arch_invariants.sh` run after this change
(`high=0, medium=0, low=0`, unchanged). Development is completely
untouched by this addition -- v1 sessions need the `Secure` cookie
attribute (plan #14), meaningless without HTTPS.

**Real defects found only by building/testing, not review** (11th-13th
across this project's whole S5/S6 body of work): (1) the new
`test_web_handlers_auth.cpp` segfaulted under a wrapped-route test case --
root-caused via a full ASan+UBSan rebuild to a test-authoring mistake, not
a production bug (`req.user_ctx` must be the wrapper's own captured
binding pointer for a wrapped route, matching what a real dispatch would
place there, not an arbitrary pointer); (2) `auth_logout_post_handler_v1()`'s
clear-cookie buffer was 64 bytes against a real 73-byte-including-NUL
requirement, caught because `build_session_cookie_clear_header()`
correctly detected the truncation and returned false rather than silently
truncating; (3) the real `idf.py build` failed with `'httpd_handler_t'
does not name a type` -- that name is this project's OWN host-mock
invention (`web_handler_common.hpp`), not a real symbol the actual
`esp_http_server.h` declares (`httpd_uri_t::handler` is declared inline
there, with no portable named typedef at all); fixed with
`decltype(httpd_uri_t{}.handler)`, portable by construction.

**Tests**: `test/host/test_capability.cpp` (11 assertions),
`test/host/test_route_authorization.cpp` (14 assertions),
`test/host/test_web_handlers_auth.cpp` (real end-to-end coverage through
*captured real registrations* -- the test's own `httpd_register_uri_handler`
mock stores every registered `httpd_uri_t` and each test case invokes the
captured `.handler` directly, exercising the real
`authenticated_dispatch_trampoline` for the two wrapped routes, never a
direct call to the handler functions themselves).

Full repository host suite: **106/106** passing (103 pre-existing + 3 new
test executables). `cppcheck --enable=warning,style,performance,portability`
reports zero findings against all 8 changed/new C++ files (one real style
finding fixed; one finding consciously suppressed with a documented
`cppcheck-suppress` + comment, because applying it would have broken the
real ESP-IDF build -- `authorize_v1_request()`'s `req` parameter is passed
straight through to the real, non-const-parameter
`httpd_req_get_cookie_val()`/`httpd_req_get_hdr_value_str()` on
`ESP_PLATFORM`). `check_arch_invariants.sh` reports `high=0, medium=0,
low=0`. Two full real `idf.py build`s succeeded end-to-end: an ordinary
build and, after the `httpd_handler_t` fix, a `ZGW_PRODUCTION_BUILD=1`
build (1205/1205 steps) -- the build that actually compiles and links
`register_web_routes_v1()`/`register_authenticated_uri_handler_v1()` for
the production listener for the first time.

**Honest, named limits, not hidden**: no external HTTP client or real
hardware exercise of these routes this round (no new HIL session --
verification depth is host-test + real-target compile/link, the same bar
Section 2.7's own #7 sub-slice was originally held to before HIL access
existed). `GET /api/v1/auth/session` delivers the CSRF token in the JSON
response body, a real judgment call the plan text does not pin down a
transport for. No rate limiting on `POST /api/v1/auth/login` yet (plan
#28, not started) -- a real brute-force exposure until that cluster
lands. No audit logging of login/logout/denied-authorization events yet
(plan #30/#31, same not-started cluster) -- `capability_token()` already
produces the exact action label that work will want.

Evidence: `implementation-evidence/S6-authorization-basic-completion.json`.

### 2.12 Physical-presence grant primitive (plan #20, #21)

Plan text: *"join, remove, Wi-Fi credential replacement, certificate
rotation, OTA, RCP and factory reset require a recent one-time
physical-presence grant."* (#20) *"Grant is created only from trusted
GPIO/button event, has maximum 60-second lifetime, is bound to gateway
boot/session/action class and is consumed once."* (#21)

**Scope, chosen explicitly by the user** (`AskUserQuestion`, over "grant +
a first real route" and "go straight to HIL"): the grant primitive and
its GPIO source only, wired into nothing. No route requires a grant yet;
none of plan #20's seven action classes are gated on anything from this
sub-slice.

**New `components/app_hal/include/hal_button.h`/`hal_button.c`**:
`hal_button_is_pressed()` -- a raw, instantaneous GPIO level read, no
debounce or edge-detection of its own (mirrors `commissioning_window.hpp`'s
own already-documented "a caller with a real button task would call this
once it has one" precedent -- that caller doesn't exist yet for this
grant either). Reads `CONFIG_ZGW_PHYSICAL_PRESENCE_BUTTON_GPIO` (new
Kconfig int in the "Security" submenu, default 9), configured with the
internal pull-up enabled, active-low. GPIO9 as the default is not
assumed: confirmed via Espressif's own official ESP32-C6-DevKitC-1
documentation (WebSearch) that it is the real BOOT button, a strapping
pin only during power-up/reset and safely readable as an ordinary input
afterward; the Kconfig range (0-30) is grounded in the real
`SOC_GPIO_PIN_COUNT` (31) read from `espressif/idf:release-v5.5`'s own
`soc_caps.h`. Host builds: mockable via `hal_button_test.h`, default
"not pressed" (fail closed).

**New `components/service/include/physical_presence_grant.hpp`/`.cpp`**:
`PhysicalPresenceActionClass` (plan #20's exact seven values),
`physical_presence_grant_create()`/`_is_valid()`/`_consume()`. Bound to
all three dimensions plan #21 names simultaneously -- boot (RAM-only
storage, free), session (exact `session_id_hex` match, both-empty
counting as a deliberate "not session-scoped" match), and action class
(exact enum match). `_consume()` is the real one-time-use enforcement:
atomically re-checks validity and deactivates the grant only on success,
so an identical immediate second call always fails. A second `_create()`
call replaces rather than rejects an already-active grant, the same
"plan names no already-active error" resolution
`commissioning_window_start()` already established for its own analogous
case.

**Tests**: `test_hal_button.cpp` (3 assertions), `test_physical_presence_grant.cpp`
(19 assertions across 10 scenarios, including the 60-second boundary,
the clock-goes-backward guard, and a failed `_consume()` not disturbing a
still-valid grant).

Full repository host suite: **108/108** passing. `cppcheck` and
`check_arch_invariants.sh` both clean. Real `idf.py build` (development
profile) succeeded, 1204/1204 steps, confirmed by grepping the build log
directly for both new object files rather than trusting the step count
alone.

**Honest, named limits**: nothing calls either new module yet -- no
debounce/edge-detection task, no route gated on a grant. No HIL exercise
of the real GPIO9 read this round, despite real ESP32-C6 hardware being
connected and available -- an explicit user choice, not a capability gap;
compile/link against the real target is confirmed, the real button's
actual behavior is not.

Evidence: `implementation-evidence/S6-physical-presence-grant-completion.json`.

### 2.13 `POST /api/v1/auth/password`, `POST /api/v1/provisioning/enroll`, `POST /api/v1/system/factory-reset/operations` (plan #17 remainder minus certificate rotation, plan #22)

Real consumers of the physical-presence grant primitive (Section 2.12),
the first anywhere in this codebase. At the time this sub-slice was
written, every one of them would always return 403
`physical_presence_required` against a real client -- nothing yet turned
a real button press into a grant. Section 2.14 closes that gap.

> The operational runbook for `provisioning/enroll` -- gate order and
> failure-token meanings, the two manufacturing records that must be in
> storage first, and the reboot-then-press sequencing that a real
> operator gets wrong exactly once -- is
> `docs/security/ADMIN_ENROLLMENT.md`. It was written against a live HIL
> enrollment on real hardware, and records one still-unresolved defect
> (intermittent TLS handshake refusals) found in the process.

**`auth/password`**: wrapped mutation-grade (`kSecurityAdmin`). Verifies
the CURRENT credential (`verify_admin_password()`, Section 2.5) *before*
consuming the one-time grant -- a wrong-password attempt must never burn
a real installer's grant. On success, overwrites the stored
`AdminVerifierRecord` via `create_admin_verifier()`/`set_stored_admin_verifier()`.

**`provisioning/enroll`**: NOT wrapped (reached without an existing
session -- it creates the first admin credential). Gated on
`commissioning_window_is_active()` (Section 2.5's state machine, now
actually started for the first time -- `WebServer::start()` calls
`commissioning_window_start()` when
`commissioning_window_first_boot_policy_applies()`), a proof-of-possession
match (new `provisioning_secret_matches()`, constant-time, in
`provisioning_secret_provider.hpp`/`.cpp`), and the physical-presence
grant (session-less, since no session exists yet). Production additionally
requires `gateway_id_verification_allows_production_enrollment()` (Section
2.9) -- always false today, same "no manufacturing record populated yet"
reality Section 2.9 already documents; development skips this check
entirely (no manufacturing record exists there either, and development
already carries weaker trust guarantees throughout).

A real, load-bearing fix made to enable this: the provisioning secret
(`provisioning_secret_provider_get()`) is now fetched exactly ONCE, in
`WebServer::start()`, and cached for the listener's lifetime via a new
`WebRouteContext::provisioning_secret` field -- not re-fetched per
request. The development adapter draws fresh randomness on every call
(never persisted); fetching it per-request would make a real
challenge/response impossible, since the value an installer reads from
the boot-time log would never match a later request-time fetch. Caching
once is what makes "installer reads the logged value, submits it back"
work at all.

**`factory-reset/operations`**: wrapped mutation-grade (`kFactoryReset`).
Plan #22: "Factory reset additionally requires a fresh manufacturing PoP
challenge. S6 owns policy validation; S8 owns the reset journal and
erase execution." Read literally -- the POLICY half (PoP + physical
presence) is real, enforced work here, not deferred; only once both pass
does the route return `capability_unavailable` (503), the plan's own
named stub outcome for the actual erase (S8, not built).

**New `ApiV1ErrorCode` values**: `kPhysicalPresenceRequired` (403),
`kProvisioningNotActive` (409) -- distinct real conditions get distinct
stable tokens, matching plan #23.

**Real defects found only by building/testing this round** (all three
in the NEW test coverage, none in production code): (1) reusing the same
`httpd_req_t` across multiple calls through a wrapped route without
resetting `req.user_ctx` to the wrapper's own binding before each call --
the trampoline (Section 2.11) overwrites it on every ALLOWED pass
regardless of what the real handler decides afterward; (2) creating
physical-presence grants/commissioning windows with an arbitrary fixed
timestamp instead of the real `hal_time_now_ms()` the handler itself
checks against; (3) the test's own `httpd_req_recv` mock destructively
drains the simulated request body as it is read, so a second call reusing
a now-empty body silently failed. All three fixed in the test file only,
confirmed via a full ASan+UBSan rebuild.

**Tests**: extended `test_web_handlers_auth.cpp` (real success and
failure paths for all three routes, via captured real registrations --
see Section 2.11's own note on why this is the real test methodology
here). Full host suite: **108/108** (unchanged executable count). `cppcheck`
and `check_arch_invariants.sh` both clean. Real `idf.py build` succeeded
both profiles: development (1204/1204) and `ZGW_PRODUCTION_BUILD=1`
(1207/1207 -- the build that actually compiles enroll's production-only
gateway-identity check and `register_web_routes_v1()` itself).

**Certificate rotation is explicitly NOT part of this sub-slice.** Real
recon into plan #12's "bounded local listener/handshake verification"
and "retain the previous confirmed slot through one successful reboot"
rollback requirement found a genuinely large, novel scope: new mbedtls
*client*-side TLS code (this project has only ever written server-side
validation, Section 2.10), a new persisted multi-boot activation/rollback
state machine (conceptually similar to but separate from this project's
existing OTA rollback mechanism), and a temporary second local listener
instance for the self-test -- comparable in size to everything else built
in this entire session combined, and not meaningfully host-testable (the
self-test and reboot-rollback protocol need real hardware). Flagged
rather than attempted blind; remains the one open item in the
"Authorization and physical presence" cluster's #17 route list besides
the wiring gap named above.

Evidence: `implementation-evidence/S6-auth-password-enroll-factory-reset-stub-completion.json`.

### 2.14 Real button-to-grant wiring, HIL-confirmed (plan #21's "created only from trusted GPIO/button event")

Closes the wiring gap Sections 2.12/2.13 both named: `hal_button_is_pressed()`
now has a real caller, and a genuine physical button press on the
connected ESP32-C6 was captured creating a grant over real serial output
-- not simulated, not asserted from source reading alone.

**`components/web_ui/web_server.cpp`**: a new `physical_presence_button_poll_callback()`,
dispatched every 50ms by a periodic `esp_timer` (production profile only,
started once in `WebServer::start()`). Requires 3 consecutive "pressed"
reads (~150ms sustained -- comfortably past real mechanical bounce,
which runs a few ms to a few tens of ms) before calling
`physical_presence_grant_create_from_button()`; an `armed` flag requires
a full release-then-press cycle before it will fire again, so holding the
button down creates exactly one grant, not a stream of them. Timer
creation failure is logged, not fatal to the listener -- every
grant-gated route already fails closed on its own with no grant present.

**New `physical_presence_grant_create_from_button()`** (added to the
Section 2.12 primitive): an "ambient" grant matching ANY action class and
ANY session, including none. A single GPIO edge carries no information
about which of plan #20's seven action classes, or which caller session,
an installer intends -- exactly the same problem WPS-style "press then
act" flows solve the same way. Still respects the same 60-second lifetime
and one-time `_consume()` semantics as an explicitly-bound grant; a
second `_create()`/`_create_from_button()` call always fully replaces
rather than merges with whatever grant existed before. `active` is
written last in both create functions specifically because the button
poller is now a genuinely concurrent writer against `WebServer`'s own
single httpd worker task reading `PhysicalPresenceGrantState` -- the
worst case of that ordering is a spuriously-still-invalid grant for one
poll cycle, never a false positive.

**Two real, hardware-only-discoverable defects found this round** (both
fixed, neither guessed):

1. **`httpd_register_uri_handler: no slots left for registering handler`**
   -- the production listener failed to start on the real device.
   `config.max_uri_handlers` was `24`, set long before this session;
   the real production route total is 44 (24 legacy + 20 v1, this
   sub-slice's own new auth routes among them). Neither the host httpd
   mock (accepts every registration unconditionally) nor any compile/link
   check can catch a runtime resource-limit exhaustion like this --
   found only by booting the real firmware. Fixed: raised to `56` (12
   slots of real headroom for near-future routes, certificate rotation
   among them).
2. **`Deferred Zigbee start task creation failed`** -- heap exhaustion.
   The first implementation used a dedicated FreeRTOS task
   (`xTaskCreate`, 3072-byte stack) for button polling. Real heap
   diagnostics (`heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)`,
   temporarily added and then removed once the fix was confirmed) showed
   free heap right before the pre-existing deferred-Zigbee-start task's
   own `xTaskCreate` call had dropped to ~4500 bytes total / 2240 bytes
   largest contiguous block -- below the 4096 bytes that task's own stack
   needs. MQTT client init alone consumes roughly 17KB on this device
   (pre-existing, unrelated to this session); the new button task's stack
   was what pushed an already-marginal budget over the edge, confirmed by
   comparison against an earlier same-session HIL round where the
   identical Zigbee task creation succeeded before this task existed.
   Fixed by replacing the dedicated task with the `esp_timer` callback
   described above -- `esp_timer` callbacks dispatch from the system's
   own shared timer task, costing zero new task stack. Verified: free
   heap right before Zigbee task creation went from a failing 4500/2240
   bytes to a passing 7856/7680 bytes, and the task creates successfully
   again.

**A third defect, caught by `test/target`'s own CI build** (not HIL --
the same class of gap Section 2.1's own `security_bounds.cpp` fallback
table was written to close): `hal_button.c` used
`CONFIG_ZGW_PHYSICAL_PRESENCE_BUTTON_GPIO` unconditionally, but
`test/target` is a separate ESP-IDF project with its own `main/` that
never sees the root `main/Kconfig.projbuild`, so the macro is undeclared
there. Fixed with the same `#ifndef`-fallback pattern already established
in this codebase, defaulting to GPIO9 -- the Kconfig option's own real
default, not an arbitrary stand-in.

**Real HIL verification, ESP32-C6-DevKitC-1, Flash Encryption
(Development mode)**: full boot captured over real serial (non-interactive
pyserial, DTR/RTS reset) -- production HTTPS listener starts with
`max_uri_handlers=56`, commissioning window starts on first-boot policy,
provisioning secret fetched once, deferred Zigbee task creates
successfully, zero panics. A second capture, this time with the user
physically pressing and releasing the real BOOT button (GPIO9) on the
connected board, shows exactly one log line, no bounce/double-fire:

```
I (1362795) web_server: Physical presence grant created from a trusted button press
```

This confirms the full chain end to end on real hardware, from a real
physical action: GPIO9 electrical level -> `hal_button_is_pressed()` ->
`esp_timer` debounce callback -> `physical_presence_grant_create_from_button()`
-> log line.

**Tests**: 3 real test-authoring bugs found via ASan+UBSan rebuild while
extending route coverage (stale `req.user_ctx` reused across calls
through a wrapped route, wrong clock basis for grant/window creation in
tests, the test's own `httpd_req_recv` mock destructively draining
`g_request_body` on reuse) -- all fixed in test code only. Full host
suite: **108/108**. `cppcheck` and `check_arch_invariants.sh` both clean.
Real `idf.py build` production profile: 1207/1207. `test/target` build
(the CI job the third defect above was caught by): clean after the fix,
OTA slot size check PASSED (66% usage).

**Honest, named limits**: no physical-presence-gated route
(`auth/password`, `provisioning/enroll`, `factory-reset/operations`) was
exercised end-to-end over a real HTTPS request against the real device
this round -- only the button-to-grant half of the chain was HIL-verified;
the route-side consumption of a button-created grant is host-test-covered
(mocked `hal_button`) but not HIL-exercised. The debounce constants (50ms
poll, 3 consecutive reads) were chosen from general mechanical-switch
bounce characteristics, not measured against this specific button/board
with an oscilloscope -- the real press test confirms they work in
practice (one clean event, no double-fire) but does not establish a
safety margin against a worse-bouncing unit. Certificate rotation (#12)
remains the one fully-deferred item in this cluster; see Section 2.13's
own recon notes.

Evidence: `implementation-evidence/S6-physical-presence-button-wiring-completion.json`.

### 2.15 Certificate rotation: active-slot state machine, bounded self-test and `POST /api/v1/security/certificates/operations` (plan #12/FD-17)

Plan #12/FD-17: *"Certificate storage has encrypted `current` and `next`
slots plus one atomic active-slot reference. Rotation is authenticated,
requires physical presence, validates key/certificate/SAN/issuer/expiry,
starts a bounded local verification using `next`, atomically switches the
active reference only after validation, and retains the previous
confirmed slot until one successful reboot/post-activation check
completes... Power loss or failed verification selects the last
confirmed `current` slot before listener enablement."*

Built across two passes in the same session. **Pass 1, scope chosen
explicitly by the user** (`AskUserQuestion`, over "everything including
the bounded self-test and `esp_restart()`"): the active-slot state
machine and the route's real policy checks (decode, offline X.509
validation, physical presence, staging-slot write) only -- the route
always returned 503 `capability_unavailable` on the policy-pass path, the
same "policy real, terminal mechanism honestly stubbed" pattern
Section 2.13's factory-reset/operations route already established for
S8. **Pass 2** (user: "далі") completed the chain: the bounded local
handshake self-test, real atomic activation, and a scheduled reboot.

**Real recon finding that reduced this feature's original scope
estimate**: `hal_ota.c` already performs real client-side TLS (an HTTPS
`esp_http_client` GET, pinning a CA via `cert_pem`, for OTA image
download) -- so the "bounded local listener/handshake verification" step
did not need hand-written raw mbedtls client code from scratch, as
earlier recon (Section 2.13's own notes) assumed; it reuses this
project's existing, already-working `esp_http_client`/esp-tls client path
against a temporary local `httpd_ssl` listener.

**New `components/service/include/cert_rotation_state.hpp`/`.cpp`**:
`tls_provisioning_storage_port.hpp` (plan S5 #13) already provides two
PHYSICAL slots (`kCurrent`/`kNext`); what plan #12 additionally wants --
named explicitly as "S6's job" in that module's own header comment -- is
a separately-persisted ACTIVE-SLOT REFERENCE, so "atomically switch the
active reference" is a single `u32` NVS write (inherits atomicity from
ESP-IDF's own commit semantics, the exact reasoning
`reset_journal_storage_port.hpp` already established) rather than copying
cert/key bytes between slots. Encoded as 2 bits (active slot, pending-
confirmation flag) under a new `tls_active_state` key in the existing
`kTlsIdentity` namespace (registry bumped from 5 to 6 key patterns). A
rotation always stages a new candidate into the COMPLEMENT of whatever
slot is currently active (`cert_rotation_staging_slot()`), so "the
previous confirmed slot" plan #12 wants retained needs no separate field
-- it is always the complement of the now-active slot. Default state
(nothing ever written): active = `kCurrent`, confirmed -- exactly
Section 2.7's original, pre-#12 behavior, so this module is purely
additive on any device that has never rotated.

`cert_rotation_activate(slot)`: the atomic switch itself, rejects a slot
that is not the current staging slot (refuses to "activate" an
already-active slot, or to re-arm mid-rotation). Sets confirmation =
pending. `cert_rotation_confirm_pending_with_result(bool)`: pure,
host-testable state-machine core (dependency-injection on the
already-computed validation outcome, matching `commissioning_window.hpp`'s
own explicit-`now_ms` precedent) -- confirms in place on success, or
rolls back to the complement slot on failure, always leaving confirmation
resolved (never left pending). `cert_rotation_confirm_pending(dns_san,
uri_san)`: the real entry point, re-reads the active slot's own material
and re-validates via `hal_tls_validate_certificate()` (ESP_PLATFORM-only,
fails closed unconditionally on host -- same boundary Section 2.10's own
validator already established) before delegating to the pure core above.

**`web_server.cpp` wiring**: `start_production_https()` calls
`cert_rotation_confirm_pending()` once per boot (before reading which
slot to load) and reads `cert_rotation_active_slot_for_listener_start()`
instead of a hardcoded `TlsCertificateSlot::kCurrent`. This is the real
mechanism behind plan #12's "one successful reboot/post-activation
check": the reboot the route schedules on activation (below) is exactly
what triggers this call on the very next boot.

**New `components/web_ui/include/cert_rotation_self_test.hpp`/`.cpp`**
(pass 2): `cert_rotation_bounded_self_test()` starts a temporary,
minimal second `httpd_ssl` listener (4096-byte stack, 1 URI handler, 1
open socket -- the library's own conservative default, nothing like
production's 20480-byte/56-handler budget) bound to the CANDIDATE
material on a dedicated non-production port (`8443`), makes one real
HTTPS request against it over loopback via `esp_http_client`, and tears
the listener down unconditionally before returning. The self-test
client's identity check is decoupled from its connection target via
`esp_http_client_config_t`'s own documented `common_name` field: the
socket connects to `127.0.0.1` (no network/mDNS dependency), but the
presented certificate must still match this gateway's real production
DNS SAN and chain to the real product CA -- the same identity a genuine
remote client would check, not a weakened loopback-only check.
`schedule_cert_rotation_reboot()` defers `esp_restart()` by 2 seconds via
a one-shot `esp_timer` (`ESP_TIMER_TASK` dispatch, no new task/stack
cost, same reasoning already established for the periodic button-poll
timer) so the route's own HTTP response has time to actually flush over
the socket before the device restarts. Both functions are ESP_PLATFORM-
only real logic with an unconditionally-defined, fail-closed/no-op host
branch -- callers never need `#ifdef ESP_PLATFORM`, matching
`hal_tls_validate_certificate()`'s own established convention.

**`POST /api/v1/security/certificates/operations`** (`web_handlers_
auth.cpp`): wrapped mutation-grade (`kSecurityAdmin`, same grade as
`auth/password`). Accepts `certificate_pem_hex`/`private_key_pem_hex`
(hex-encoded PEM text, reusing the existing `decode_hex()` helper --
avoids this project's own ad-hoc JSON reader needing to understand
embedded-newline escaping inside a JSON string, the same reasoning
`provisioning/enroll`'s proof-of-possession field already established).
Full chain, in order, mirroring `auth/password`'s own "a bad attempt must
never burn the grant" principle at every gate: decode -> reject a
candidate missing the mbedtls PEM-convention trailing NUL byte -> read
the product CA -> build this gateway's own expected DNS/URI SAN ->
offline-validate via `hal_tls_validate_certificate()` -> the bounded
local handshake self-test -> only THEN consume the physical-presence
grant (`kCertificateRotation`) -> write the now-validated candidate into
`cert_rotation_staging_slot()` for real (encrypted, S5's write gate) ->
atomically activate it (`cert_rotation_activate()`, confirmation
pending) -> send a success response -> schedule the reboot. An installer
can now actually rotate the production certificate through this route.

**Tests**: `test_cert_rotation_state.cpp` (13 scenarios covering every
state transition, including a full two-rotation confirm/roll-back cycle
and the real `cert_rotation_confirm_pending()` entry point's
host-fails-closed behavior). `test_web_handlers_auth.cpp` extended: every
rejection this route applies BEFORE the real validator call is
host-tested (malformed JSON, bad hex, missing NUL terminator, CA never
provisioned); the validator call itself is an unconditional host gate
(same boundary as every other real-crypto path in this project), so the
self-test/activation/reboot path is real code, confirmed to compile and
link against the real ESP32-C6 target, but not host-exercised -- the
same split `cert_rotation_state.hpp`'s own tests already document.

Full host suite: **109/109** (108 pre-existing + 1 new executable,
`test_cert_rotation_state`; `test_web_handlers_auth`'s own assertion
count grew in place, not a new executable). `cppcheck` and
`check_arch_invariants.sh` both clean. Real `idf.py build` succeeded both
profiles (development and `ZGW_PRODUCTION_BUILD=1`), confirming the new
`esp_http_client` component dependency (`web_ui`'s `CMakeLists.txt`
`REQUIRES` list, needed for the self-test client) links correctly and
that binary size headroom remains healthy (32% free flash in both
profiles).

**Honest, named limits**: no live HIL exercise of this route yet --
running a SECOND `httpd_ssl` listener concurrently with the already-live
production one, on a device this same session already found to have a
tight post-boot heap budget (Section 2.14's own real heap-exhaustion
bug), is a genuine, not-yet-measured resource-pressure question that only
real hardware can answer; triggering it also means a real, deliberate
device reboot via `esp_restart()`, an outward-facing action on hardware
the user is actively relying on for other testing. Compile/link
verification (this section's own bar) confirms the code is correct and
resource-bounded by construction (minimal stack/socket/handler budget for
the temporary listener), but not that the real device survives running
two listeners at once under real memory pressure. The debounce/retry
behavior of a FAILED self-test (candidate written to staging slot but
never activated, exactly like a rejected offline-validation candidate)
was reasoned through but not exercised against a real broken candidate on
real hardware either.

Evidence: `implementation-evidence/S6-cert-rotation-state-and-route-completion.json`.

### 2.16 Live HIL round for certificate rotation: four real boot-stability defects found and fixed; the route not reached in *this* round (Sections 2.17-2.18 continue the thread)

User authorized a live HIL round for Section 2.15's route (`AskUserQuestion`:
"Так, робимо HIL зараз"). The route was never exercised: real hardware
crashed before `WebServer::start()` on every attempt, for reasons entirely
unrelated to certificate rotation's own code. Chasing each crash to a real,
evidence-backed root cause (UART core dumps decoded via `idf.py
coredump-info` and, when that itself failed under heap pressure,
`riscv32-esp-elf-addr2line` against the real build ELF; real
`heap_caps_get_free_size()`/`heap_caps_get_largest_free_block()`
diagnostics) found and fixed four independent, hardware-only-discoverable
defects in this project's own boot-time task/stack management --
none previously exercised because no earlier HIL round on this project had
ever reached a real, connected Wi-Fi station event before this one:

- **Stack overflow / silent LP-RAM fallback in `ServiceRuntime::start()`'s
  task creation.** Plain `xTaskCreate()`'s default allocator can silently
  place a task's stack in LP-RAM (~15KB, meant for ULP code) instead of
  real SRAM when the preferred pool is fragmented -- confirmed by reading
  ESP-IDF's own `components/heap/port/esp32c6/memory_layout.c`: on
  ESP32-C6, `MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT` alone does not exclude
  LP-RAM. Fixed with a new `components/app_hal/hal_memory.c`/`.h`
  (`hal_alloc_internal_sram()`, adding `MALLOC_CAP_DMA` -- the one
  capability real SRAM's high-priority set has that LP-RAM's sets never
  do) plus `xTaskCreateStatic()` for all of `ServiceRuntime::start()`'s
  tasks, and reordering `g_runtime.start()` in `app_main.cpp` to run
  immediately after Wi-Fi driver init (which alone consumes ~65KB) rather
  than after Wi-Fi connect.
- **A second, different stack overflow**, in `CoreRegistry::snapshot_copy()`'s
  own by-value local (`CoreState` is ~2.5KB with `kMaxDevices=64`) --
  unreachable until the very first time this project's code ever processed
  a real Wi-Fi "connected" event on hardware. `persist_current_core_state()`
  already had a `static`-local fix for this exact bug class, scoped only to
  itself; `CoreRegistry::snapshot_copy()` (a shared public API with other,
  concurrent callers, so it cannot safely use the same `static` fix) still
  had its own copy. Fixed by increasing `kRuntimeTaskStackSize`.
- **The same LP-RAM-fallback class as the first defect, in a different
  component**: `hal_zigbee.c`'s `zigbee_main` task, created via plain
  `xTaskCreate()`, was never covered by that fix (a different component,
  `app_hal`, not `service`). Fixed with the same `hal_alloc_internal_sram()`
  + `xTaskCreateStatic()` pattern.
- **A regression from the second defect's own fix**: the larger
  `kRuntimeTaskStackSize` needed to fix the `snapshot_copy()` overflow did
  not fit the PRODUCTION + Flash-Encryption-Development profile's own,
  much tighter heap budget on this hardware (roughly half the free heap at
  `app_main()` start compared to the same code without Flash Encryption) --
  `service_runtime` failed to start at all there, a worse outcome than the
  original overflow. Re-tuned to a value real HIL confirmed fits both
  profiles. (Section 2.17 later removed the underlying heap shortage
  entirely, making this constant comfortable again rather than marginal.)

Evidence: `implementation-evidence/S6-cert-rotation-hil-round-stack-and-heap-fixes.json`.

### 2.17 The Wi-Fi station failure: one memory bug behind three symptoms

> Full engineering post-mortem, including every hypothesis that was tested and
> ruled out and the diagnostic method that finally worked:
> [`docs/process/POSTMORTEM_WIFI_HEAP_STARVATION.md`](../process/POSTMORTEM_WIFI_HEAP_STARVATION.md).
> This section covers only the security-relevant summary.

The Wi-Fi station connectivity failure that blocked Section 2.16's whole
HIL round -- 100% reproducible on the one board provisioned with Flash
Encryption, the only board that can run the production HTTPS profile the
certificate-rotation route lives behind -- was root-caused and fixed.
Several earlier hypotheses were tested and ruled out along the way
(router-side MAC filtering, corruption of the `phy_init` RF-calibration
partition, and an ESP-IDF 5.5.2-vs-5.5.5 toolchain regression -- an
earlier finding claiming 5.5.2 fixed it was retested with current code
and did NOT reproduce, superseding that claim).

**Root cause: ~166KB of permanently reserved `.bss` in four global
objects**, dominated by `MqttBridge`'s `pending_publications_[192]`
staging queue (192 x 452 bytes = ~85KB). That queue is purely transient
-- filled by one sync pass, then drained to empty in batches of 8 in the
same task iteration -- yet it was reserved for the device's entire
uptime. On the production + Flash-Encryption profile (whose own overhead
roughly halves the available heap) this left the Wi-Fi driver with
**280-476 bytes of DMA-capable heap, largest free block 92-272 bytes**,
measured directly on hardware at the moment of disconnect. The driver
simply could not allocate a buffer for its own management frames.

**One cause, three symptoms.** The driver's `m f auth` / `m f assoc req`
/ `m f beacon` messages are frame-buffer *allocation failures* (it prints
the explicit form, `alloc eb len=752 type=4 fail`, on the SoftAP path).
The SoftAP crash and the station auth timeout were the same exhaustion
failing at whichever stage allocated first; ESP-IDF coexistence, which
needs its own memory, consumed enough extra to push the failure earlier
(auth rather than the WPA2 4-way handshake). This also explains every
previously puzzling property: perfectly deterministic, and completely
unaffected by signal strength, distance, router configuration, RF
calibration, or toolchain version.

**Diagnosis method** (worth repeating for this class of bug): a
line-by-line boot-log diff against a known-good board isolated
coexistence as the one consistent difference; disabling it moved the
failure from auth to the 4-way handshake (with RSSI -59 / SNR 38,
proving RF was healthy) rather than fixing it; a temporary heap probe in
the STA-disconnected handler then measured the actual exhaustion; and
`idf.py size-components` plus a parse of the `.map` file's largest
`.bss` symbols identified the responsible objects.

**Fixes**: `pending_publications_` became a lifecycle-scoped allocation
claimed in `MqttBridge::start()` -- which runs only once the network is
already up, precisely when the Wi-Fi association buffers are no longer
needed -- and released in `stop()`, with capacity and drop-when-full
semantics unchanged (a failed allocation degrades to skipping
publications). `PersistedStateStore::probe_slot()`'s ~2.2KB stack local
became a function-local `static`, fixing a stack overflow on the
network-up persistence path that only became reachable once association
started working. `kRuntimeTaskStackSize` returned to 16384 now that the
freed memory makes real margin affordable again.

**Verified on real hardware** with coexistence **re-enabled** (the
configuration the product actually needs, since the gateway runs Wi-Fi
and Zigbee together): one boot with no reboots at all across a 55-second
window (the board previously crash-looped 10-16 times per window), Wi-Fi
connected, DHCP address obtained, zero disconnects, zero crashes, mDNS
advertising, and the production HTTPS listener correctly failing closed
on absent certificate material. Heap at boot rose from 66 KiB to 148 KiB.

The certificate-rotation route still has no live HIL exercise, but it is
no longer blocked by connectivity. The board's TLS identity material --
wiped by an earlier flash-erase during this same investigation -- has
since been re-provisioned (fresh EC P-256 product CA and device leaf
carrying the two SAN values this device's own validator requires), and
Section 2.18 records what happened once the listener could actually
complete a handshake.

Evidence: `implementation-evidence/S6-cert-rotation-hil-round-stack-and-heap-fixes.json`.

### 2.18 First authenticated control-plane exercise on real hardware

With Section 2.17's Wi-Fi fix in place the device associated and served
HTTPS, and immediately failed a second time: every TLS connection died in
`mbedtls_ssl_setup()` with `-0x7F00` (`MBEDTLS_ERR_SSL_ALLOC_FAILED`).
The cause was the same defect class one layer up -- `libweb_ui.a` held
**57 780 bytes of `.bss`**, 45 188 of them six `static` scratch buffers
inside `certificates_operations_post_handler_v1()` (Section 2.15's own
route). They were `static` for a defensible reason, being far too large
for the handler task's stack, but that made them permanently resident for
a route that runs a handful of times in a device's life, and a TLS
session had nothing left to allocate from. They are now grouped into one
`calloc`/`free` RAII-owned struct claimed on handler entry, with
`auto&` aliases preserving every use site and `sizeof(...)`; allocation
failure returns `503 no_capacity` through the golden matrix rather than
crashing.

That unblocked the first real authenticated exercise of this stage's
work:

- `mbedtls_ssl_setup` errors **0**; the certificate-rotation route
  answered over the network with `401 unauthenticated` -- confirming the
  route is registered, reachable and correctly refusing an unauthenticated
  caller, though a complete authenticated rotation still has not been
  driven.
- A full **administrator enrollment** through
  `POST /api/v1/provisioning/enroll`, traversing every gate this stage
  built: commissioning window (Section 2.5), proof of possession
  (Section 2.4/2.5), the physical-presence grant minted by a real button
  press (Sections 2.12/2.14), and the manufacturing GatewayId check
  (Section 2.9). Device-side confirmation:
  `set_blob ok, key='admin_verifier' len=52`.
- `POST /api/v1/auth/login` then returned a session cookie with the
  Section 2.6 attributes (`Secure; HttpOnly; SameSite=Strict;
  Path=/api/v1`), and `GET /api/v1/auth/session` returned a CSRF token
  and the full capability set (Section 2.11).

**Section 2.9's fail-closed design was confirmed the hard way.** The
production identity gate rejected enrollment with `capability_unavailable`
until a manufacturing record existed. Both manufacturing records
(`mfg_pop` and the 6-byte `mfg_gateway_id`) had to be written by a
temporary hook standing in for the factory step this project cannot
perform (`BLOCKED_SECURITY_PROVISIONING`, unchanged). **That hook has been
removed and the board reflashed**; left in place it would rewrite both
records on every boot, which would reduce the anti-cloning check to a
no-op -- a cloned image would simply re-record whatever MAC it found
itself running on.

One ordering property is worth stating because it is a real operational
cost: the presence grant is **consumed before** the identity check runs,
so a failed identity check spends a single-use 60-second button press.
The runbook -- gate order, failure tokens, preconditions and the
reboot-then-press sequencing -- is
[`docs/security/ADMIN_ENROLLMENT.md`](ADMIN_ENROLLMENT.md).

**Open defect from this round.** Roughly one HTTPS connection in five is
refused mid-handshake (`unexpected eof` client-side, `-0x0050` /
`esp_tls_create_server_session failed` device-side). It is not a memory
symptom -- it persists with both `.bss` fixes in place. The suspected
mechanism is `web_server.cpp`'s `max_open_sockets = 4` together with
`lru_purge_enable = true`, but **neither has been varied and the
hypothesis is untested**. An earlier reading in this investigation
declared TLS stable on a 6/6 sample; that is superseded.

Evidence: `implementation-evidence/S6-cert-rotation-hil-round-stack-and-heap-fixes.json`.

## 3. What is explicitly deferred

Every other S6 required change remains unimplemented -- these four
sub-slices are exactly the foundation and the first named removals the
plan's own text calls for, nothing more. **"Provisioning and credentials"
(#1-#6) is now fully done** (Section 2.4 for #1/#4/#6, Section 2.5 for
#2/#3/#5). Section 2.5's original "nothing calls it yet" caveat --
that the `ProvisioningSecretProvider` port and the commissioning-window
state machine existed and were tested but had no real consumer -- **no
longer applies**: Section 2.13's `provisioning/enroll` route reads the
provisioning secret and gates on the commissioning window being active,
and Section 2.18 exercised both against real hardware end to end.

- **HTTPS and sessions** (#7-#17): all 11 items implemented -- #7/#9/#10/
  #11/#13-#17 fully, #8 in its scoped, local-self-consistency form only
  (Section 2.9's own text is explicit that this is not the same guarantee
  as fleet-wide duplicate-enrollment detection, which remains out of
  reach without a manufacturing backend this project does not have), and
  #12 (Section 2.15) real end to end but still not HIL-exercised as a
  complete rotation -- the route is now confirmed registered and
  reachable on hardware, answering `401 unauthenticated` over the network
  (Section 2.18), but no authenticated request carrying a new
  certificate/key pair has been driven through self-test and active-slot
  swap; see also that section's own honest-limits note on the real,
  not-yet-measured resource-pressure question of running two
  `httpd_ssl` listeners at once on real hardware. A live HIL round was
  attempted (Section 2.16) and found/fixed four real, unrelated boot-time
  defects, then root-caused and fixed the Wi-Fi station failure that had
  blocked it (Section 2.17: ~166KB of permanently reserved `.bss` starving
  the Wi-Fi driver of DMA-capable heap). The device now boots stably with
  working Wi-Fi under the production profile; the route still awaits its
  HIL exercise, now only pending re-provisioning of that board's TLS
  identity material. Sections 2.6-2.10 still
  have no *automatic* production provisioning pipeline -- the HIL
  session's real certificate (Section 2.10's own addendum) was loaded by
  a one-off manual exercise onto one specific test device, not a
  repeatable flow any real device goes through; Section 2.9's
  manufacturing-record gate is similarly still never satisfied by
  anything automatic.
- **Authorization and physical presence** (#18-#23): all done -- Sections
  2.11-2.15. #12/certificate rotation (shared with the cluster above)
  is real end to end but still needs its own HIL round, same caveat as
  above (Section 2.16).
- **Strict request parsing** (#24-#27): the `cJSON`-backed
  `StrictJsonObjectReader`, per-command schema validation, fuzz corpus.
- **Rate limiting and audit** (#28-#31): the rate limiter itself, the
  audit ring.
- **MQTT production security** (#32-#37): `hal_mqtt_config_t` trust-mode
  extension, production MQTT TLS/trust Kconfig, broker ACL, credential
  redaction.

All of these will consume `security_bounds()` (Section 2.1) instead of
reading `CONFIG_ZGW_*` macros directly, per the plan's own "typed accessor
is the only application-facing source for tunables" text.

### 3.1 Named, tracked responsibility: three independent copies of the same range table

`main/Kconfig.projbuild`'s `range`/`default` declarations,
`components/service/include/security_bounds.hpp`'s `kRanges` table (in
`security_bounds.cpp`), and `scripts/verify_production_security_profile.py`'s
`TUNABLE_RANGES` all declare the same ten (minimum, maximum,
approved_default) triples independently, by hand. Nothing currently
verifies the three stay in sync automatically -- a future edit to one
without the other two would not be caught by any test in this repository
today. Flagged directly rather than hidden; a follow-up could generate
one of the three from another, or add a cross-check test that parses
`main/Kconfig.projbuild` and compares it against the other two tables.

### 3.2 S6's own precondition, partially open

S6's plan text preconditions include *"hardware security evidence... are
available"* -- per `implementation-evidence/S5-completion.json`'s own
`s6_readiness` note, the *software-only* half of S5 is complete, but real
Secure Boot/Flash Encryption/NVS Encryption hardware readback remains
`BLOCKED_SECURITY_PROVISIONING` (no manufacturing/eFuse environment in
this sandbox). This sub-slice proceeded on the basis that S6's
software-only foundation work does not itself require that hardware
evidence to exist yet -- a judgment call, not a plan-text resolution;
worth the project owner's attention if it matters before S6 is considered
releasable.

### 3.3 Provisioning passphrase delivery: an explicit interim decision, not a final design

Section 2.4's passphrase is delivered to the installer by logging it via
`ESP_LOGI` at AP start -- a deliberate, user-confirmed interim decision
(this device has no display, only UART, and no real manufacturing/
labeling process exists yet), not a resolved production design.
Section 2.5's `provisioning_secret_provider_get()` development adapter
now exists and uses the identical posture for its own one-time secret. A
real production delivery channel (a printed label with a per-device QR
code or similar manufacturing step, most likely tied to the same
manufacturing proof-of-possession material the production adapter reads)
remains a named, tracked follow-up for both secrets -- not resolved by
either sub-slice.

### 3.4 Session capacity policy and the unused `kSessionSeed` namespace: judgment calls, not plan-text resolutions

Section 2.6's `session_store_create()` rejects a 5th concurrent session
outright (`kFull`) rather than evicting the oldest/least-recently-active
one -- plan #13's "bounded store of four" does not itself say which
behavior is correct at capacity. The conservative choice (never
involuntarily log out an already-authenticated session) was made
unilaterally, matching this project's fail-closed posture elsewhere, but
is genuinely revisable -- worth the project owner's attention if
UX considerations (a single admin locked out of their own 5th browser
tab) should outweigh it once real usage exists.

Separately, `nvs_namespace_registry.cpp`'s `kSessionSeed` entry (added
speculatively during S5, still `implemented_today=false`) anticipated
session tokens being derived from persisted seed/signing material;
Section 2.6 instead generates every session ID and CSRF token fresh from
hardware RNG per session, needing no persisted seed at all. Left exactly
as-is (not implemented, not removed) -- worth the project owner's
attention if a different token-derivation design was actually intended
for `kSessionSeed`.

## 4. Environment note

Verified via `zgw-host-tools:s0` (build + full 103-test `ctest` suite) and
`zgw-host-tools:cppcheck` (zero findings) for all host-testable C/C++
logic; via `python3` directly (stdlib `unittest`, and `hashlib`/`hmac` as
independent reference-vector generators for Section 2.5's host crypto
implementation) for the verifier's own tunable-range logic and the new
crypto primitives; and via full real `idf.py build`s inside
`espressif/idf:release-v5.5` -- Section 2.2's build confirming the real
generated `sdkconfig` matches expectations and observing ESP-IDF's own
Kconfig `range` rejection behavior directly, Section 2.4's build
confirming `main/app_main.cpp`'s production wiring compiles and links
(catching a real missing-namespace-qualifier bug the host build could not
have), Section 2.5's two builds (ordinary and `ZGW_PRODUCTION_BUILD=1`)
confirming both `admin_verifier.cpp`'s real `mbedtls_pkcs5_pbkdf2_hmac_ext()`
call and `provisioning_secret_provider.cpp`'s production-only branch
compile and link against the real ESP32-C6 target, Section 2.6's build
confirming the new session-store/policy translation units compile and
link cleanly (no new ESP-IDF/mbedtls API surface of their own), and
Section 2.7's two builds confirming `web_server.cpp`'s real
`httpd_ssl_start()`/`esp_https_server` call compiles and links (this
project's Docker toolchain has no QEMU/hardware to actually run the
resulting firmware and observe a TLS handshake, so compile-and-link is
this sub-slice's full verification depth for that file, matching every
other `ESP_PLATFORM`-only HAL/composition-root file in this project), and
Section 2.8's two builds confirming `build_gateway_mdns_host()`'s
production branch (only reachable under a real `CONFIG_ZGW_PRODUCTION_PROFILE=y`
target build, unlike its development branch which the host suite already
exercises) actually compiles, and Section 2.9's single build confirming
`gateway_identity_verification.cpp` (a plain, unconditionally-compiled
service-layer module with no `CONFIG_ZGW_PRODUCTION_PROFILE` branch of its
own) compiles and links cleanly, and Section 2.10's two builds confirming
`hal_tls_certificate_validator.c`'s real mbedtls X.509/PK calls compile
under an ordinary build and the full production call site (CA read, SAN
construction, validation call) compiles and links under
`ZGW_PRODUCTION_BUILD=1` -- the second of which caught a real
incomplete-type compiler error the ordinary build structurally could not
have (the affected code path is compiled out entirely outside the
production profile). No real ESP32-C6 hardware or manufacturing/eFuse
environment was needed for any sub-slice.
