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

## 2. What is implemented and verified (sub-slices 1-8: security invariants/bounded tunables/typed accessor, provisioning AP secret generation, provisioning-credentials remainder, session store + cookie/CSRF/CORS policy, production HTTPS listener, production mDNS host derivation, gateway identity self-consistency verification, TLS certificate chain/SAN/expiry/key validation -- plan #1-11, #13-16)

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

## 3. What is explicitly deferred

Every other S6 required change remains unimplemented -- these four
sub-slices are exactly the foundation and the first named removals the
plan's own text calls for, nothing more. **"Provisioning and credentials"
(#1-#6) is now fully done** (Section 2.4 for #1/#4/#6, Section 2.5 for
#2/#3/#5) -- but note Section 2.5's own "nothing calls it yet" caveats:
the `ProvisioningSecretProvider` port and the commissioning-window state
machine both exist and are tested, but neither is wired into any real
consumer (no enrollment flow reads the provisioning secret; no request
handler gates behavior on the commissioning window being active) -- that
wiring is deferred to the sub-slices below that actually need it.

- **HTTPS and sessions, remainder** (#12, #17): authenticated
  physical-presence-protected certificate rotation (#12, blocked on the
  "Authorization and physical presence" cluster below -- needs
  physical-presence grants that do not exist yet), and the seven exact
  authentication routes (#17, needs both the session store from Section
  2.6 AND the central authorization middleware from the next cluster).
  Every other "HTTPS and sessions" item (#7, #9, #10, #11, #13-#16) is
  now implemented -- #8 in its scoped, local-self-consistency form only
  (Section 2.9's own text is explicit that this is not the same guarantee
  as fleet-wide duplicate-enrollment detection, which remains out of
  reach without a manufacturing backend this project does not have). The
  HTTPS listener (Section 2.7), mDNS host (Section 2.8), gateway-identity
  verification (Section 2.9) and certificate validation (Section 2.10)
  all still have no real end-to-end production effect today, each for
  its own already-documented reason -- no real certificate exists
  anywhere to satisfy Section 2.7/2.10's fail-closed gates, and no
  manufacturing record exists to satisfy Section 2.9's. Sections 2.6-2.10.
- **Authorization and physical presence** (#18-#23): the capability set,
  central authorization middleware, physical-presence grants, factory-reset
  policy validation.
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
