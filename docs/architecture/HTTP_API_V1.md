<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 Alex.K. -->

# Versioned HTTP `/api/v1` contract (S4 HTTP required changes #1-#9, #28-#30)

This document records the S4 HTTP v1 slice: what is frozen and enforced
today, and what remains explicitly out of scope for this pass. It
complements `docs/architecture/GATEWAY_IDENTITY.md` and
`docs/architecture/MATTER_ENDPOINT_IDENTITY.md` (the two earlier S4 slices)
and `docs/architecture/DEVICE_IDENTITY.md` (S2).

## 1. Problem this piece closes

Before this pass, every HTTP route was unversioned (`/api/...`), device
mutations and responses used `short_addr` as the sole device identifier,
and there was no golden status/error matrix -- error status codes and
tokens were chosen per-handler. This is the first concrete step toward the
plan's versioned, `DeviceId`-based external contract.

## 2. Scope of this pass

This document now covers two passes. The first implemented the four read
(`GET`) routes and the shared DTO/golden-matrix/registration
infrastructure every route needs. The second (this update) implements
every remaining route in the plan's canonical table except the unified
operation-status poll:

- `GET /api/v1/capabilities`
- `GET /api/v1/devices`
- `GET /api/v1/network`
- `GET /api/v1/config`
- `POST /api/v1/devices/join-window`
- `POST /api/v1/devices/{device_id}/commands/power`
- `DELETE /api/v1/devices/{device_id}`
- `PUT /api/v1/devices/{device_id}/reporting/{endpoint}/{cluster_id}`
- `POST /api/v1/network/scans`
- `POST /api/v1/network/connections`
- `PATCH /api/v1/config`
- `POST /api/v1/ota/operations`
- `POST /api/v1/rcp-update/operations`

**12 of the 13 canonical routes are implemented.** Only
`GET /api/v1/operations/{operation_id}` remains unimplemented -- see
Section 4.1, unchanged from the first pass: it needs an
`OperationResultStore` domain-unification that is a distinct unit of work.
The legacy read-alias/`410`-legacy-mutation contract (plan #7/#8) and the
bundled Web UI migration (plan #6) also remain deferred, per FD-19/plan
#29 (S4 does not touch the production route-registration path at all) --
see Sections 4.2 and 4.3.

## 3. What is implemented and tested

### 3.1 `DevicesApiDeviceSnapshot` gains canonical identity (plan #2, #3)

`components/service/include/service_runtime_api.hpp`: `DevicesApiDeviceSnapshot`
gained `device_id_hex` (pre-formatted 16-lowercase-hex-character string,
matching the `core::DeviceId::format()` contract), `has_locator` and
`locator_revision`. `short_addr` is now meaningful **only** when
`has_locator` is true.

`DevicesApiSnapshotBuilder::build()` (`devices_api_snapshot_builder.cpp`)
changed its inclusion gate from "has a `short_addr`" to "has a valid
`device_id`" (FD-01) -- a genuine behavior change, not cosmetic: a device
restored from persisted state that has not yet rejoined now correctly
*appears* in the API (its durable identity survived restore) with
`has_locator: false`, rather than being silently absent the way the old
`short_addr`-gated loop left it. `has_locator`/`short_addr`/
`locator_revision` are resolved via a new `DeviceLocatorRegistry` parameter
threaded through `DevicesApiSnapshotBuilder` -> `ReadModelCoordinator` ->
`ServiceRuntime::build_devices_api_snapshot()`, rather than echoing
`core::CoreDeviceRecord::short_addr` directly -- that raw field is not a
reliable "has a *current* locator" signal on its own (S3's restore path
carries the last known `short_addr` forward as a diagnostic, even though
the device has not actually rejoined at that address yet).

This threading touched three existing tests
(`test_service_runtime.cpp`, `test_service_runtime_lifecycle.cpp`,
`test_read_model_coordinator.cpp`) and one existing integration test
(`test_integration_web_handlers_device.cpp`), all of which posted raw
`core::CoreEvent`s with no resolved `device_id` and/or never populated
`DeviceLocatorRegistry` -- both are now required for a device to be
identity-visible and locator-visible respectively. Fixed by adding
`device_id`/`DeviceLocatorRegistry::remap()` calls to each, matching the
pattern already established for the Matter/GatewayId test suites earlier
in S4.

### 3.2 Golden status/error matrix and `schema_version` (plan #4, #5, #9)

`components/web_ui/include/web_v1_common.hpp` + `web_v1_common.cpp`:
`ApiV1ErrorCode` enumerates the plan's stable error vocabulary
(`device_not_found`, `identity_unresolved`, `device_offline`,
`stale_locator`, `capability_unavailable`, `no_capacity`, `conflict`, plus
`invalid_request`/`legacy_mutation_disabled` for cases the enumerated list
implies but does not name). `api_v1_error_status()` is the **single**
place that maps each code to an HTTP status, so no handler can invent its
own status/token pairing -- this is the "one authoritative versioned golden
status/error matrix" the plan calls for, implemented as code rather than a
separate fixture file (matching this codebase's existing convention of
inline assertions over fixture files -- there is no fixture-based test
infrastructure anywhere else in the repository to be consistent with).
`send_api_v1_error()` wraps every error response in `{"schema_version":1,
"error":"<token>"}`; every success response also carries `"schema_version":1`.
The first pass exercised only the GET-reachable subset (200 success, 503
`capability_unavailable`). The mutation routes added in this pass reach
the rest of the matrix: 202 `accepted:true` (pollable async operations --
join-window, power, delete, network scan/connect, OTA/RCP), 200
`accepted:true` (fire-and-forget queued writes with no pollable id --
config `PATCH`, reporting `PUT`), 400 `invalid_request` (body/path
validation), 404 `device_not_found`, 409 `device_offline` /`conflict`
(join window already open, OTA/RCP already in flight), and 503
`no_capacity` (request queue full). `410 legacy_mutation_disabled` remains
unreachable, since it belongs to the legacy-mutation-disable contract
deferred in Section 4.2, not to any v1 route built here.

Two new helpers split the success side by whether the operation is
pollable: `send_api_v1_accepted(req, request_id)` (202, exposes the
`request_id`/correlation id the caller would poll) and `send_api_v1_ok(req)`
(200, no id -- mirrors the legacy handlers' identical fire-and-forget
distinction for config writes and reporting-profile writes).

`resolve_short_addr_for_device_id_hex()` (new on `ServiceRuntimeApi`,
implemented in `ServiceRuntime`) is what makes the golden matrix's
device-identity errors possible from `web_ui` without that layer ever
naming a Core-namespaced symbol (INV-M030): it takes a URL-path
`device_id` hex string and returns a `DeviceIdResolveStatus`
(`kResolved`/`kMalformedHex`/`kUnknownDevice`/`kNoCurrentLocator`),
walking `CoreState.devices` for identity existence before consulting
`DeviceLocatorRegistry` for locator freshness -- so "malformed hex" (400),
"the device_id is not known" (404 `device_not_found`), and "the device_id
is known but has no current locator" (409 `device_offline`) are three
distinct, correctly-mapped outcomes rather than one flat failure.

### 3.3 Four GET handlers (plan #1)

`components/web_ui/web_handlers_v1.cpp`: `capabilities_get_handler_v1`,
`devices_get_handler_v1`, `network_get_handler_v1`, `config_get_handler_v1`,
plus `register_web_routes_v1()` and one `register_*_routes_v1()` per route
group. Bodies mirror the legacy handlers' snprintf-based JSON construction
(no new serialization abstraction introduced, to stay consistent with the
rest of `web_ui`) with two additions: the `schema_version` envelope, and
(devices only) `device_id`/nullable `short_addr`/`locator_revision` instead
of a bare `short_addr`.

Tested in `test/host/test_web_handlers_v1.cpp` (capabilities/network/config
-- these three do not call `esp_timer_get_time()`, so they are host-testable
via the established direct-`#include`-of-the-.cpp pattern) and
`test/integration/test_integration_web_handlers_v1.cpp` (devices -- needs
`esp_timer_get_time()` and a fuller `ServiceRuntime` fixture, matching the
precedent already set by the legacy `/api/devices` integration test). The
devices test specifically proves the nullable-`short_addr` contract: a
device with a resolved identity but no locator shows `"short_addr":null`
and `"locator_revision":null`, while a fully resolved device shows both as
concrete values.

### 3.5 Nine mutation handlers (plan #1)

`device_join_window_post_handler_v1`, `device_power_post_handler_v1`,
`device_delete_handler_v1`, `device_reporting_put_handler_v1`,
`network_scan_post_handler_v1`, `network_connect_post_handler_v1`,
`config_patch_handler_v1`, `ota_operations_post_handler_v1`,
`rcp_update_operations_post_handler_v1` -- each mirrors its legacy
counterpart's body-parsing and validation exactly, but resolves any
device-identity path segment via `resolve_short_addr_for_device_id_hex()`
(Section 3.2) and routes every outcome through the golden-matrix helpers
instead of ad hoc status/message strings.

Four routes carry a `{device_id}` (and, for reporting, `{endpoint}`/
`{cluster_id}`) URL path segment: `POST .../commands/power`,
`DELETE /api/v1/devices/{device_id}`, `PUT .../reporting/{endpoint}/
{cluster_id}`, and (implicitly, as the non-matching case) `join-window`
which is registered as an **exact** path so it is never mistaken for a
`{device_id}`. Parsing these needed two additions this pass:

- **`extract_uri_device_id_hex()` / `extract_uri_device_id_and_reporting_
  segments()`** (`web_v1_common.cpp`) -- manual `strncmp`/`strtoul`
  pointer-arithmetic parsers (no `sscanf`, matching this codebase's
  existing convention), validating exact prefix/suffix literals and
  segment-boundary characters explicitly.
- **A `uri` field on the host `httpd_req_t` shim**
  (`web_handler_common.hpp`, non-`ESP_PLATFORM` branch) -- previously
  absent, since no pre-v1 handler ever needed a path parameter (every
  identifier came from a JSON body or an MQTT topic string). Tests set it
  directly. The host shim's `httpd_method_t` also gained `HTTP_PUT`/
  `HTTP_PATCH`/`HTTP_DELETE` (it previously had only `HTTP_GET`/
  `HTTP_POST`).

Reporting-profile writes (`device_reporting_put_handler_v1`) write a
`ConfigManager::ReportingProfile` keyed by `DeviceId`, from `web_ui` code
that must not name `core::DeviceId` (INV-M030). This reuses the `auto`-
typed-variable pattern already established in the S3 ConfigManager rekey
pass: `const auto device_id = context->runtime->
resolve_device_id_for_short_addr(short_addr); profile.key.device_id =
device_id;` -- the type is never spelled out in adapter source text, so
the architecture gate's textual `core::` grep (`INV-M030`) passes.

OTA/RCP submit-failure mapping (`map_ota_submit_error_v1()`/
`map_rcp_submit_error_v1()`) is deliberately coarse: `OtaSubmitStatus`/
`RcpUpdateSubmitStatus` enumerate roughly ten domain-specific validation
failures (manifest/board/project/schema/signature mismatches), all of
which map to the single 400 `invalid_request` token; only `kBusy` (and,
for RCP, `kConflict`) maps to 409 `conflict`. This is a deliberate
judgment call, not an oversight: the plan's own "golden matrix" concept
implies a small, stable top-level vocabulary, not one that mirrors every
internal validation branch.

Tested in `test/host/test_web_handlers_v1.cpp` for every route except
`device_power_post_handler_v1` (the one mutation that stamps
`issued_at_ms` from `esp_timer_get_time()`, so it needs the integration
fixture) -- covered instead in
`test/integration/test_integration_web_handlers_v1.cpp` alongside the
devices GET test. Both files exercise the success path and every
reachable golden-matrix error for each route, including the two-call
sequences needed to observe async state (e.g. opening a join window then
calling `process_pending()` before asserting the second open attempt
returns 409 `conflict`, since `post_open_join_window()`/
`post_reporting_profile_write()`/`post_config_write()` only enqueue --
the visible state change happens inside `process_pending()`, not
synchronously on submit).

### 3.4 Unregistered by construction (plan #28, #29, #30)

`register_web_routes_v1()` and its seven sub-registration functions
(`register_capabilities_routes_v1`, `register_device_routes_v1`,
`register_network_routes_v1`, `register_config_routes_v1`,
`register_ota_routes_v1`, `register_rcp_routes_v1` -- the last two new in
this pass) are declared in `web_routes.hpp` and defined in
`web_handlers_v1.cpp`, but **nothing in `register_web_routes()` (the
function `main/app_main.cpp` actually calls via `WebServer::start()`)
calls any of them.** They exist only as a registrable contract, invoked
directly by host/integration tests and never by the production
route-registration path.

New architecture rule `INV-H010` enforces both halves of this: the v1
registration contract must exist (`check_present` on `web_routes.hpp`), and
`main/app_main.cpp` must never name any of the seven `register_*_routes_v1`
functions (`check_absent`, extended in this pass to add the two new OTA/RCP
registration functions to the pattern). This is the "build/static
invariant proving that no S4/S5 production artifact contains an enabled
production control-plane registration path" the plan calls for (#30),
scoped to what this pass actually built.

`register_device_routes_v1()` registers `POST /api/v1/devices/join-window`
(an exact path) **before** the three wildcard `/api/v1/devices/*` handlers
(`POST` power, `DELETE` remove, `PUT` reporting) -- real `esp_http_server`
tries registered patterns in registration order under
`httpd_uri_match_wildcard`, so the exact match must be tried first or it
would be shadowed. This ordering assumption is unverified against a real
ESP-IDF `httpd` instance in this sandbox; see Section 5.

Because v1 routes are registered on a **separate, never-invoked-in-
production** `httpd_uri_t` set, they do not consume any of the legacy
server's `max_uri_handlers` budget (currently 20 of 24 used, per the S4
recon at the start of this stage) -- host/integration tests that exercise
v1 routes do so without ever starting the legacy route set at all.

## 4. What is explicitly deferred

### 4.1 The unified operation-status poll

`GET /api/v1/operations/{operation_id}` is not implemented. It needs a
result-domain unification this repository does not have yet:
`OperationResultStore` (`operation_result_store.hpp`) tracks network/
config/OTA/RCP results as four **separate** typed result queues, each with
its own `take_*_result()` method, sharing only a single global
`request_id` counter. A generic `/operations/{id}` endpoint needs either a
discriminated union across all four domains or a caller that tries each
domain in turn; neither exists today and designing one is a distinct unit
of work from the route infrastructure this pass built. Every route this
pass added that produces a pollable `request_id`/correlation id
(join-window, power, delete, network scan/connect, OTA/RCP) returns it in
the 202 response body, so the poll route is purely additive once built --
no other route needs to change to support it later.

The path-parameter-parsing blocker recorded against this section in the
first pass (the host `httpd_req_t` shim had no `uri` field) is resolved:
Section 3.5 covers the URI parsing this pass added.

### 4.2 Legacy read-alias deprecation metadata and legacy-mutation `410 Gone`

Plan #7 (legacy `/api/...` reads proxy v1 with deprecation metadata) and
#8 (legacy mutations return `410 legacy_mutation_disabled` in production)
are not implemented. Per FD-19 and plan #29, S4 does not touch the
production route-registration path at all -- the existing legacy routes
continue exactly as before during S4. Implementing #7/#8 for real requires
either wiring v1 into the same registration path the legacy routes use
(which #29 forbids for S4) or a separate mechanism S4 does not otherwise
need; this is deferred to whichever stage actually flips the production
listener over to v1 (S6, per FD-19: "S6 is the first and only stage that
wires production management routes into the composition root").

### 4.3 Bundled Web UI migration to v1 (plan #6)

`assets/app.js` (and the rest of the bundled UI) still calls the legacy
`/api/...` routes. Migrating it is meaningless before the mutation routes
it needs (device power, join, remove, reporting) exist, so it is deferred
alongside them.

## 5. Environment limitation

Verified via the Docker host-tools workaround (`zgw-host-tools:s0` for
build/test/sanitizer; `zgw-host-tools:cppcheck` for static analysis).
ESP-IDF/`idf.py` remains unavailable. Unlike the Matter/GatewayId S4
slices, this pass adds no new `ESP_PLATFORM`-only code paths of its own
(the v1 handlers reuse `esp_http_server`/`esp_timer` exactly as the legacy
handlers already did), so it does not introduce a new BLOCKED_TOOLCHAIN
surface beyond what S0 already recorded; the real target wildcard-URI
dispatch of these routes (as opposed to calling the handler functions
directly, which is what every test here does) is unverified against a
real ESP-IDF `httpd` instance -- this specifically includes the exact-
before-wildcard registration ordering assumption in
`register_device_routes_v1()` (Section 3.4), which this sandbox cannot
exercise end-to-end since there is no real `httpd_uri_match_wildcard` to
dispatch against.
