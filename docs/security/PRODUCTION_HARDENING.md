<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 Alex.K. -->

# Production hardening: hardware security foundation

This document records Stage S5 (`docs/implementation/PRODUCTION_HARDENING_PLAN.md`,
"Establish hardware security foundation and encrypted storage"): what is
frozen and tested today, and what remains explicitly out of scope. It is
the security-focused counterpart to the `docs/architecture/*.md` series
(`HTTP_API_V1.md`, `MQTT_API_V1.md`, `GATEWAY_IDENTITY.md`,
`MATTER_ENDPOINT_IDENTITY.md`) covering S4.

## 1. Why S5 is scoped differently from every prior stage

S0-S4 could all be fully built and verified through this project's Docker
host-tools workaround (`zgw-host-tools:s0`/`:cppcheck`), because they were
either Core-layer logic, service-layer state machines, or adapter
contracts that compile and run identically on a Linux host. S5's target
artifacts (Secure Boot v2, Flash Encryption, NVS Encryption) are
eFuse-backed hardware features -- but, unlike earlier stages'
`BLOCKED_TOOLCHAIN` assumption, **ESP-IDF itself turned out to be
available in this environment via the same `espressif/idf:release-v5.5`
Docker image the project's own CI `firmware-build` job already uses**.
This meant the approved Kconfig profile and the `sdkconfig.production.esp32c6`
mechanism could be verified against a real `idf.py set-target`/`build`
run rather than documentation alone (Section 2). What genuinely remains
hardware-only -- actual Secure Boot/Flash Encryption/NVS Encryption
readback, actual eFuse burn-and-verify, anything needing a physical
ESP32-C6 -- is still `BLOCKED_SECURITY_PROVISIONING` (Section 4), since
this sandbox has no manufacturing/eFuse environment or real chip. The
plan's own preconditions anticipate exactly this split: *"if irreversible
provisioning cannot be dry-run and read back, return
`BLOCKED_SECURITY_PROVISIONING`."*

## 2. What is implemented and verified (sub-slices 1-11: production security profile, eFuse provisioning template, ESP-IDF version check, NVS namespace registry, typed secure-storage read port, runtime encryption-verified write gate, restart-safe migration scaffolding, TLS/provisioning storage interfaces, log/crash-report/evidence redaction, typed namespace erase enforcement, protected reset-journal storage port, plan #1-#5 + #9-#18 -- all ten "Encrypted storage foundation" items complete)

### 2.1 Approved Kconfig symbol list -- verified against real ESP-IDF, not just documentation

`sdkconfig.production.esp32c6` (new, a *layered* `sdkconfig.defaults`
fragment -- combined with `sdkconfig.defaults`/`sdkconfig.defaults.esp32c6`
only when a production build opts in, see Section 2.2) enables:

| Symbol | Value | Purpose |
|---|---|---|
| `CONFIG_SECURE_BOOT` | `y` | Enable hardware Secure Boot in the bootloader |
| `CONFIG_SECURE_BOOT_V2_ENABLED` | `y` | Secure Boot v2 (the only scheme ESP32-C6 supports; also the `SECURE_BOOT_VERSION` choice's own default once `SECURE_BOOT_V2_PREFERRED` is set by the SoC, but pinned explicitly rather than relied on implicitly) |
| `CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES` | *(not set)* | External signing workflow -- the production private signing key never enters this repository or CI |
| `CONFIG_SECURE_BOOT_INSECURE` | *(not set)* | Master gate for ESP-IDF's entire "Potentially Insecure" Kconfig menu |
| `CONFIG_SECURE_FLASH_ENC_ENABLED` | `y` | Flash Encryption on boot |
| `CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE` | `y` | Release mode (irreversible; development mode allows re-flashing plaintext) |
| `CONFIG_NVS_ENCRYPTION` | `y` | NVS Encryption |
| `CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC` | `y` | NVS encryption key sourced from the Flash Encryption key rather than a separate HMAC eFuse key block |
| `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` | `y` | Already shipped in `sdkconfig.defaults.esp32c6` (OTA app rollback) |
| `CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK` | `y` | Ties the eFuse-recorded secure version to what the bootloader accepts |
| `CONFIG_SECURE_ENABLE_SECURE_ROM_DL_MODE` | `y` | UART ROM download mode permanently switched to Secure mode (already ESP-IDF's own implicit default once Secure Boot/Flash Encryption release mode are on, pinned explicitly here) |
| `CONFIG_SECURE_BOOT_ALLOW_JTAG` | *(must not be `y`)* | Leaves JTAG enabled once Secure Boot/Flash Encryption are on, defeating both |
| `CONFIG_SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT` | *(must not be `y`)* | The other half of the `SECURE_FLASH_ENCRYPTION_MODE` choice -- checked explicitly for a clearer diagnostic even though `MODE_RELEASE=y` above already implies it by choice-exclusivity |

**Verification trail** -- this list was checked two ways, and the second
pass caught real mistakes the first one missed:

1. Documentation pass: cross-referenced against the official ESP-IDF
   5.5.1 docs for ESP32-C6 --
   [Security Features Enablement Workflows](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/security/security-features-enablement-workflows.html),
   [Secure Boot v2](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/security/secure-boot-v2.html),
   [Configuration Options Reference](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/kconfig-reference.html).
2. **Real-build pass**: pulled `espressif/idf:release-v5.5` (the exact
   image tag this project's own CI `firmware-build` job already uses),
   grepped the actual Kconfig source inside it
   (`components/bootloader/Kconfig.projbuild`,
   `components/nvs_flash/Kconfig`, `components/nvs_sec_provider/Kconfig`,
   `components/bootloader/Kconfig.app_rollback`,
   `components/partition_table/Kconfig.projbuild`), and ran
   `idf.py -B build-prod-test set-target esp32c6` (and a full
   `idf.py build`) against this repository's real, layered
   `sdkconfig.defaults`/`sdkconfig.defaults.esp32c6`/`sdkconfig.production.esp32c6`.

This second pass found and fixed two real mistakes:

- **`CONFIG_ESP32C6_DISABLE_JTAG` does not exist anywhere in ESP-IDF
  5.5's Kconfig tree.** It was a web-search-sourced guess (a real search
  engine summary described "the default behavior" correctly but attached
  a symbol name to it that doesn't exist). The real symbol,
  `CONFIG_SECURE_BOOT_ALLOW_JTAG`, was found by grepping the actual
  Kconfig source and reading its `depends on`/help text directly.
- **`CONFIG_SECURE_BOOT_INSECURE` was missing from the forbidden list
  entirely** -- it is the master gate for the whole "Potentially
  Insecure" Kconfig menu that `SECURE_BOOT_ALLOW_JTAG` lives inside;
  without also forbidding it, a sufficiently determined misconfiguration
  could route around the JTAG check via a different symbol under the same
  menu.

Everything else in the required-symbol table above (the 8-symbol core
list) turned out to be exactly right on the first pass -- confirmed by
grepping the real Kconfig source, not just re-trusting the earlier
documentation-only pass.

### 2.2 `CMakeLists.txt` bug found and fixed: the production fragment was never actually loaded

The very first real-build attempt (`idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32c6;sdkconfig.production.esp32c6" set-target esp32c6`,
following this repository's own documented pattern for the `sdkconfig.mqtt.local`
layering example) **silently ignored the production file** -- the build
log showed only `Loading defaults file .../sdkconfig.defaults...` and
`.../sdkconfig.defaults.esp32c6...`, never the third file, and the
generated `sdkconfig` had every approved symbol either absent or
explicitly "not set."

Root cause: top-level `CMakeLists.txt` does `set(SDKCONFIG_DEFAULTS
"sdkconfig.defaults")` unconditionally (no `CACHE`), which -- in CMake --
creates a normal variable that shadows any command-line `-D` cache
override within that directory scope. The `-D SDKCONFIG_DEFAULTS=...`
override was silently discarded before ESP-IDF's `project.cmake` ever saw
it.

**Fix applied**: `CMakeLists.txt` now appends `sdkconfig.production.esp32c6`
to `SDKCONFIG_DEFAULTS` only when `ZGW_PRODUCTION_BUILD` is set in the
build environment:

```cmake
if(DEFINED ENV{ZGW_PRODUCTION_BUILD} AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.production.esp32c6")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.production.esp32c6")
endif()
```

A production build now uses `ZGW_PRODUCTION_BUILD=1 idf.py set-target esp32c6 build`.
Ordinary/development/HIL builds (including the existing `firmware-build`
and `target-hal-tests-build` CI jobs, which set no such variable) are
completely unaffected -- confirmed by re-running `idf.py set-target
esp32c6` **without** `ZGW_PRODUCTION_BUILD` and observing only the
original two defaults files load, exactly as before this change (plan
#2: development and production profiles stay separate).

**Re-verified after the fix**: with `ZGW_PRODUCTION_BUILD=1` set, all
three defaults files loaded (`Loading defaults file .../sdkconfig.production.esp32c6...`
now appears), the `idf.py set-target esp32c6` step completed with no
warnings or Kconfig conflicts, and running
`scripts/verify_production_security_profile.py` against the **real
generated `sdkconfig`** (not a simulated concatenation) reported:

```
OK: sdkconfig matches the approved production security profile.
```

### 2.3 ESP-IDF version discrepancy -- resolved as a project policy decision, now enforced (plan #4, second half)

The plan's own text requires "the resolved ESP-IDF version is not exactly
5.5.2" to fail a production build. The `espressif/idf:release-v5.5` image
-- the exact tag this project's CI already uses for `firmware-build` and
`target-hal-tests-build` -- resolves to **ESP-IDF 5.5.5**
(`IDF_VERSION_MAJOR=5`, `MINOR=5`, `PATCH=5`, confirmed from
`tools/cmake/version.cmake` inside the image), not 5.5.2. `release-v5.5`
is a branch-tracking tag that follows the latest 5.5.x patch release, so
this drift is expected to continue as Espressif ships further 5.5.x
patches. A second, independent confirmation of the same drift surfaced
while capturing this section's fixture (Section 2.3.1 below): this
repository's own git-tracked `dependencies.lock` records `idf: version:
5.5.2` for the ESP-IDF Component Manager, and a real `idf.py reconfigure`
against `espressif/idf:release-v5.5` wants to rewrite that line to `5.5.5`
-- i.e. the 5.5.2 pin exists in more than just the plan's prose, and a
real build already disagrees with it.

This was a real, named discrepancy between the plan's frozen intent and
this project's actual CI configuration that a prior pass of this sub-slice
deliberately did not resolve unilaterally (pinning to an exact patch vs.
tracking the release branch is a project policy choice, not a technical
correctness question). **The user has since decided the policy**: allow
any `5.5.x` patch, matching how this project's CI actually consumes
ESP-IDF (tracking `release-v5.5`, not a frozen patch), rather than
tightening to an exact pin or loosening `dependencies.lock`'s own
`5.5.2`. `verify_production_security_profile.py` now implements this
(Section 2.3.1) -- plan #4 is fully implemented as of this sub-slice.

#### 2.3.1 `verify_production_security_profile.py --project-description` (plan #4, second half)

`verify_idf_version()`/`read_idf_version_from_project_description()`
check the resolved ESP-IDF version against `EXPECTED_IDF_VERSION_MAJOR=5`/
`EXPECTED_IDF_VERSION_MINOR=5`, deliberately leaving `PATCH` unconstrained.
The version is sourced from a real `idf.py reconfigure`/`build`-generated
`project_description.json`'s `git_revision` field -- ESP-IDF's own `git
describe` output for whatever `IDF_PATH` checkout actually produced the
build (e.g. `v5.5.5-316-g1a1a5aa6513`), not a static copy of
`version.cmake` that could point at a different or stale checkout. This
mirrors the Kconfig check's own philosophy (verify against what a real
build actually produced, Section 2.2/2.4) rather than trusting a
documentation- or source-tree-only value.

**Grounding**: ran `idf.py -B build-ver-test set-target esp32c6 && idf.py
-B build-ver-test reconfigure` (a fast, build-avoiding cmake-configure-only
pass -- no need for a full `idf.py build` just to generate
`project_description.json`) inside `espressif/idf:release-v5.5` against
this repository, confirmed `project_description.json` has no `idf_version`
field (an earlier assumption checked and found wrong before committing to
the `git_revision` field instead) but does have `git_revision:
"v5.5.5-316-g1a1a5aa6513"`, and captured a trimmed real copy as
`scripts/testdata/project_description_esp32c6_real.json` (real key/value
pairs, not fabricated).

The `--project-description PATH` CLI flag is optional -- omitting it skips
the version check entirely (unchanged behavior for a bare sdkconfig with
no accompanying build directory, e.g. the `RepositoryLayeredProfileTest`'s
simulated concatenation, which has no real build directory to point at).

Tested in `scripts/test_verify_production_security_profile.py`: parsing
of the real `git describe` format plus plain-tag and `-dirty`-suffixed
variants, matching-minor/different-patch passing (5.5.5, 5.5.2 and 5.5.0
all pass), wrong-major and wrong-minor failing, an unparseable version
string failing with a clear diagnostic, a missing `git_revision` key
raising loudly rather than silently passing, and `main()`'s
`--project-description` wiring (skip / pass / fail / missing-file exit
code 2) -- 15 new tests, all against the real captured fixture where
applicable, bringing this script's suite to 31/31 passing.

### 2.4 Blocker found AND fixed: bootloader binary too large for the default partition table offset

Running a **full** `ZGW_PRODUCTION_BUILD=1 idf.py build` (not just
`set-target`) initially failed during the bootloader link step:

```
Error: Bootloader binary size 0xc000 bytes is too large for partition table offset 0x8000.
Bootloader binary can be maximum 0x8000 (32768) bytes unless the partition table offset
is increased in the Partition Table section of the project configuration menu.
```

Enabling Secure Boot v2 + Flash Encryption compiles significant extra
crypto code into the bootloader (RSA/ECDSA verification, flash encryption
key handling), growing it past the default 32KB (`0x8000`) budget
reserved between the bootloader's start and the default partition-table
offset (also `0x8000` by ESP-IDF default -- the two numbers coincide,
which is what makes the default configuration exactly too tight).

**A second, related requirement surfaced from the same real-Kconfig-source
grep that found Section 2.1's JTAG correction**:
`CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC`'s own help text reads
*"Requires a separate 'nvs_keys' partition (which will be encrypted by
flash encryption) for storing the NVS encryption keys"* -- `partitions.csv`
has no such partition, so NVS Encryption would have failed at a later
build/runtime stage even after the offset fix alone.

**Fix applied**: `partitions.production.csv` (new) -- a *production-only*
partition table, selected via `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME`/
`CONFIG_PARTITION_TABLE_OFFSET` in `sdkconfig.production.esp32c6`, leaving
the shared root `partitions.csv` (and `test/target/partitions.csv`, used
by the real, currently-configured self-hosted HIL runner) completely
untouched -- mirroring the same `ZGW_PRODUCTION_BUILD`-gated separation
principle as the sdkconfig fragment itself (plan #2). It moves the
partition table to `0x10000` and inserts a `4K` `nvs_keys` partition right
after `nvs`; `ota_0`/`ota_1`/`zb_storage`/`zb_fct`/`coredump`/`storage`
keep their exact original offsets and sizes -- the original gap between
`phy_init` and `ota_0` had enough slack (`0xE000`) to absorb both changes
(net growth `0x9000`) without moving or resizing anything downstream.

**Re-verified end-to-end** with a full `ZGW_PRODUCTION_BUILD=1 idf.py build`
(not just `set-target`):

- Build completed successfully: `1181/1181` steps, `zigbee_gateway.bin`
  generated, ending in `App built but not signed. Sign app before
  flashing` -- exactly the intended external-signing state (Section
  2.1's `SECURE_BOOT_BUILD_SIGNED_BINARIES` choice).
- `zigbee_gateway.bin` is `0x1c0000` bytes against a `0x280000`-byte
  `ota_0` slot -- `0xc0000` bytes (30%) free.
- `scripts/check_ota_slot_size.sh build-prod-test partitions.production.csv`
  reported `70%` usage and **PASSED** unmodified (it already accepted a
  partition-table-path argument).
- `scripts/verify_production_security_profile.py` against the real
  generated `sdkconfig` still reported **OK** (the partition-table
  symbols added are outside the approved/forbidden Kconfig lists it
  checks; no interaction).
- The **actual flashed partition table**, parsed back out of the built
  binary with ESP-IDF's own `gen_esp32part.py` (not re-derived from the
  source CSV -- the real, binary-encoded table the bootloader would read),
  matched `partitions.production.csv` exactly: `nvs` at `0x11000`,
  `nvs_keys` at `0x17000` (`4K`), `otadata` at `0x18000`, `phy_init` at
  `0x1a000`, `ota_0` at `0x20000` -- confirming the layout is correct not
  just in the source file but in what ESP-IDF actually built from it.

A full production build now succeeds end-to-end from this sandbox, using
only the Docker-available ESP-IDF toolchain -- the remaining gap to an
actual flashable, working device is exclusively the real-hardware/eFuse
work in Section 3.3, not anything left broken in the software build
itself.

### 2.5 `scripts/verify_production_security_profile.py` (plan #3)

A pure text-parsing tool: reads a generated `sdkconfig` file
(`CONFIG_X=value` / `# CONFIG_X is not set` lines) and fails with a
specific, itemized diagnostic if any approved symbol is missing or has
the wrong value, or if any forbidden symbol is present. Exit code `0` on
a clean match, `1` on any violation, `2` if the file cannot be read.

Tested in `scripts/test_verify_production_security_profile.py` (stdlib
`unittest`, matching this repository's zero-extra-dependency host-test
convention): parsing of assignment/not-set/quoted-string/comment lines,
every required-missing/wrong-value/forbidden-present violation path
(including the two corrected forbidden symbols from Section 2.1),
multiple simultaneous violations all being reported, `main()`'s exit
codes, and the repository's own three layered files passing together (a
simulated concatenation -- Section 2.2's real `idf.py` run is the
stronger, actually-target-verified proof; the simulation is kept as a
fast-running regression check for future drift). `security-profile-verifier-tests`
runs this suite in CI on every push/PR -- pure Python, no ESP-IDF
container needed.

### 2.6 `scripts/efuse_provisioning_template.py` -- machine-readable eFuse provisioning template (plan #5)

Plan #5 requires: *"Define a machine-readable eFuse provisioning template
containing chip identity, secure-boot digest slots, flash-encryption
state, anti-rollback/security-version fields, JTAG/download policy and
protection status."* This sub-slice implements exactly that -- the
template/schema itself, as a pure data-transformation module with no
hardware interaction -- and deliberately stops there. Plan #6 (the
two-phase dry-run/burn workflow), #7 (device quarantine for unexpected
eFuse state) and #8's burn-time enforcement all need a real
manufacturing/eFuse environment to dry-run and read back against, which
this sandbox does not have, so they remain `BLOCKED_SECURITY_PROVISIONING`
(Section 3.2). What #8 *does* require of the template format itself --
*"store only redacted public digest/evidence... never place production
private keys in the repository"* -- is implemented directly in this
module (`redact_key_material`), since that is a property of the record
format, not of the burn workflow.

**Grounding methodology**: every field name in the template was checked
against a REAL ESP32-C6 eFuse summary, not invented or guessed --
avoiding the exact class of mistake Section 2.1 found and fixed
(`CONFIG_ESP32C6_DISABLE_JTAG`). ESP-IDF ships `espefuse.py` with a
`--virt` flag that runs its own full eFuse emulation with no real
hardware required:

```
espefuse.py --chip esp32c6 --virt summary --format json
```

Run inside the same `espressif/idf:release-v5.5` image used for Section 2,
this produced a real, 64-field JSON structure (each field carrying
`name`/`category`/`block`/`bit_len`/`description`/`efuse_type`/
`raw_value`/`value`/`readable`/`writeable`/`word`/`pos`), captured verbatim
as `scripts/testdata/espefuse_esp32c6_virtual_summary.json`. This fixture
is the ground truth both the template's field lists and its test suite
validate against.

**Schema** (`scripts/efuse_provisioning_template.py`): a provisioning
record is `schema_version` + `gateway_id` (the canonical 12-lowercase-hex
FD-17 GatewayId, tying the record to the same factory-base-MAC-derived
identity used everywhere else in this project) + `captured_at` + five
category sections, mapped directly from plan #5's own required category
list onto real `espefuse.py` field names:

| Section | Real eFuse fields |
|---|---|
| `chip_identity` | `MAC`, `WAFER_VERSION_MAJOR`, `WAFER_VERSION_MINOR`, `PKG_VERSION`, `OPTIONAL_UNIQUE_ID`, `BLK_VERSION_MAJOR`, `BLK_VERSION_MINOR` |
| `secure_boot` | `SECURE_BOOT_EN`, `KEY_PURPOSE_0..5`, `SECURE_BOOT_KEY_REVOKE0..2`, `SECURE_BOOT_AGGRESSIVE_REVOKE`, `SECURE_BOOT_DISABLE_FAST_WAKE` |
| `flash_encryption` | `SPI_BOOT_CRYPT_CNT`, `DIS_DOWNLOAD_MANUAL_ENCRYPT` |
| `anti_rollback` | `SECURE_VERSION` |
| `jtag_download_policy` | `DIS_PAD_JTAG`, `SOFT_DIS_JTAG`, `JTAG_SEL_ENABLE`, `DIS_DOWNLOAD_MODE`, `DIS_FORCE_DOWNLOAD`, `ENABLE_SECURITY_DOWNLOAD`, `DIS_DOWNLOAD_ICACHE`, `SPI_DOWNLOAD_MSPI_DIS` |

Each projected field keeps only `value`/`raw_value`/`readable`/`writeable`
("protection status", per plan #5) -- redundant `espefuse.py` metadata
(`bit_len`/`word`/`pos`/`category`/`description`/`efuse_type`/`name`) is
dropped since a provisioning record doesn't need to repeat it.

**Key-material redaction** (plan #5's "secure-boot digest slots" + plan
#8's redaction requirement): the six raw key blocks (`BLOCK_KEY0..5`) go
through a separate `key_material_digests` section via
`redact_key_material()`, which returns `readable`/`writeable` plus a
SHA-256 digest of the raw key bytes -- never the key value or raw_value
itself. `validate_provisioning_record()` enforces this defensively: a
record is rejected if any `key_material_digests` entry is missing its
`redacted: true` flag or its digest, *or* if a `value`/`raw_value` key
leaks into it at all (catching an accidental future regression, not just
a missing field).

**CLI**: `efuse_provisioning_template.py build <espefuse_summary.json>
--gateway-id <hex> --captured-at <iso8601> --output <path>` projects a
real `espefuse.py summary --format json` file into a provisioning record
(and self-validates the record it just built before writing it -- a
build producing an invalid record is treated as a bug in the script
itself, not a data problem); `efuse_provisioning_template.py validate
<record.json>` re-validates an existing record. Manually verified: a
`build` then `validate` round trip against the real captured fixture
produces a record with all five sections populated and key material
redacted.

**Tests** (`scripts/test_efuse_provisioning_template.py`, stdlib
`unittest`, matching this repository's zero-extra-dependency host-test
convention): 18 tests across 6 classes, all passing
(`Ran 18 tests in 0.015s / OK`). Notably:

- `RealFixtureTest.test_all_referenced_fields_exist_in_real_summary` --
  asserts every field name this module references is actually present in
  the real captured `espefuse.py` fixture, an explicit guard against
  reintroducing the invented-symbol-name mistake found in Section 2.1.
- `test_key_material_fields_are_real_256_bit_blocks` -- confirms the six
  `BLOCK_KEY*` fields are genuinely 256-bit `security`-category fields in
  the real fixture, not a guess about their shape.
- `test_missing_espefuse_field_is_simply_omitted_not_fabricated` -- a
  sparse input summary produces a record that omits the missing field
  (never fabricates a value) and fails validation loudly rather than
  silently claiming completeness.
- `test_leaked_raw_key_material_fails` / `test_unredacted_key_material_fails`
  -- confirm the redaction defense-in-depth checks actually fire.
- `MainCliTest.test_build_then_validate_round_trip` -- exercises the CLI
  end-to-end against the real fixture via `template.main([...])`.

`.github/workflows/ci.yml`'s existing `security-profile-verifier-tests`
job now also runs `python3 -m unittest scripts/test_efuse_provisioning_template.py -v`
on every push/PR -- pure Python, no ESP-IDF container needed, alongside
the Section 2.5 verifier's own test step.

### 2.7 `nvs_namespace_registry.hpp`/`.cpp` -- encrypted-storage namespace ownership and FD-21 classification (plan #9 + #15)

Plan #9 requires: *"Create explicit encrypted NVS namespaces/ownership for
Wi-Fi credentials, MQTT credentials/trust references, admin verifier, TLS
private key/certificate reference, session seed and manufacturing
provisioning records."* Plan #15 requires: *"Classify every eFuse/
partition/NVS namespace/key as `PRESERVE_ON_FACTORY_RESET`,
`ERASE_ON_FACTORY_RESET` or `RESET_JOURNAL_ONLY`; an unclassified namespace
blocks S8 reset implementation."* This sub-slice implements both together,
as one pure-data, host-testable C++ registry
(`components/service/include/nvs_namespace_registry.hpp` +
`components/service/nvs_namespace_registry.cpp`) with no hardware
dependency and no behavior change to any existing NVS call site.

It deliberately does not implement the rest of the "Encrypted storage
foundation" cluster -- #10 (typed storage-port results), #11 (restart-safe
migration scaffolding), #12 (runtime encryption-verified write gate), #13
(TLS/provisioning storage interfaces), #14 (log/evidence redaction), #16/#17
(actual namespace erase/preserve enforcement) and #18 (the reset-journal
storage port itself) -- each of those needs this registry to exist first
and is its own coherent unit of work, matching how #1-#4 and #5 were each
their own sub-slice earlier in this document.

**Grounding**: every namespace entry is derived from a real, grepped
inventory of this repository's actual `hal_nvs_*` call sites
(`config_manager.cpp`, `connectivity_manager.cpp`, `network_manager.cpp`,
`matter_endpoint_registry.cpp`, `persisted_state_store.cpp`,
`state_persistence_coordinator.cpp`, `ota_manager.cpp`,
`effect_executor.cpp`), not invented -- including config_manager.cpp's two
different dynamically-built per-reporting-profile key prefixes
(`rptp_%c%02u` for the current schema and the older `cfg_rpt_%c%02u`,
found by reading `build_profile_nvs_key`/`build_legacy_v2_profile_nvs_key`
directly rather than guessing from the schema-version migration logic
around them). FD-21 (`docs/implementation/PRODUCTION_HARDENING_PLAN.md`,
"Restart-safe factory reset") is the classification source of truth,
quoted directly in the header's own comments; every one of FD-21's named
categories maps onto exactly one registry entry except three "Preserve"
items that are not NVS namespaces at all and so have no entry: eFuse/
security state and Secure Boot/Flash/NVS encryption key material live in
eFuse and the hardware-managed `nvs_keys` partition (Section 2.4), not an
app-owned NVS namespace, and the factory `GatewayId` is derived at runtime
from the eFuse-backed factory base MAC
(`hal_identity_get_factory_base_mac`) and is never written to NVS -- all
three are preserved trivially, by construction, with nothing app-erasable
to classify.

**Schema**: 12 `NvsNamespaceId` entries (`kWifiCredentials`,
`kMqttCredentials`, `kAdminVerifier`, `kTlsIdentity`, `kSessionSeed`,
`kManufacturingProvisioning` -- plan #9's list, in the order the plan names
them -- plus `kZigbeeNetworkDeviceReporting`, `kMatterEndpointState`,
`kCoreDeviceState`, `kOperationJournalDiagnostics`,
`kLegacyMigrationTombstone` for pre-existing data, and `kResetJournal` for
plan #18's future storage port). Each `NvsNamespaceEntry` carries: the
target ESP-IDF NVS namespace string (still the single shared
`"zigbee_gateway"` legacy namespace for every pre-existing entry --
`hal_nvs.c` hardcodes that one namespace for every call site today, and
separating them into real distinct namespaces is #11's migration job, not
this registry's); an owner description; an `NvsResetClassification`
(`kPreserveOnFactoryReset` / `kEraseOnFactoryReset` / `kResetJournalOnly`);
`encryption_required` (true for exactly plan #9's six namespaces, matching
the plan's explicit list, not the five pre-existing ones); an
`implemented_today` flag; and a bounded array of `NvsKeyPattern` (exact key
or key-prefix) entries recording what actually lives there today.

**Validation** (the plan's own Tests section names this exact check: *"NVS
namespace ownership and duplicate-key inventory"*):
`validate_nvs_namespace_registry()` walks every entry pair and flags a
`kDuplicateKeyClaim` violation if their key patterns could ever match the
same real key (exact-exact, exact-inside-prefix, or overlapping prefixes),
plus a `kImplementedFlagKeyCountMismatch` violation if `implemented_today`
disagrees with whether the entry actually lists any key patterns. The
pairwise primitive (`nvs_namespace_entries_conflict()`) is exposed
separately so tests can prove the *detection logic itself* fires on a
synthetic conflict, not just that the shipped registry happens to already
be clean -- this repository's own established discipline (S5's Kconfig
and eFuse-field work both caught real mistakes only once verified against
something real rather than assumed correct).

**Tests** (`test/host/test_nvs_namespace_registry.cpp`, bare `assert()`,
matching this repository's C++ host-test convention, wired into
`components/service/CMakeLists.txt` and `test/host/CMakeLists.txt`): the
shipped registry has zero violations; every entry's array index matches
its `NvsNamespaceId` value; `find_nvs_namespace_entry`/
`find_owning_namespace_for_key` resolve exact and prefix keys correctly
(including the two real reporting-profile prefixes) and return null for an
unknown/empty/null key; `nvs_namespace_entries_conflict()` actually detects
exact-vs-exact, exact-vs-prefix and overlapping-prefix conflicts on
synthetic entries, and does not flag disjoint keys; every FD-21 Preserve/
Erase/reset-journal-only classification is pinned per-namespace against
the plan text directly; `encryption_required` matches plan #9's six-item
list exactly; and `ResetJournalState`'s four values match FD-21's exact
`requested -> erasing -> reinitialized -> commissioning_ready` sequence.
14 tests, all passing. `cppcheck --enable=warning,performance,portability`
reports zero findings against the new `.cpp`, and the full existing
84-test host suite (`ctest`) still passes 84/84 after this addition --
confirming the change is additive and does not alter any existing NVS call
site's behavior. `check_arch_invariants.sh` passes unchanged (`high=0,
medium=0, low=0`); this addition introduces no new HAL contract or route
registration for that script to check.

### 2.8 `secure_storage_port.hpp`/`.cpp` -- typed fail-closed storage-read results (plan #10)

Plan #10 requires: *"Add storage ports that return typed `available`,
`not_provisioned`, `corrupt` and `unavailable` results; production callers
must fail closed on any non-available state."* The "Contracts and
invariants" section names the failure case directly: *"Secure-storage
failure returns `secure_storage_unavailable` and maps to HTTP 503 when
exposed later."* This sub-slice implements exactly the typed-result read
path, built directly on Section 2.7's namespace ownership registry.

It deliberately does not implement #11 (restart-safe migration
scaffolding -- no `hal_nvs.c` call site has changed; every value still
lives in the single hardcoded `"zigbee_gateway"` namespace exactly as
before), #12 (a runtime NVS-Encryption-verified write gate), #13 (TLS/
provisioning storage interfaces) or #14 (redaction). Each needs this port
to exist first and is its own later sub-slice.

**Design**: `SecureStorageStatus` (`kAvailable`/`kNotProvisioned`/
`kCorrupt`/`kUnavailable`) plus four free functions:

- `secure_storage_classify_raw_status(hal_nvs_status_t)` maps a raw
  `hal_nvs_get_*` result onto the four states: `HAL_NVS_STATUS_OK` ->
  `kAvailable` (tentatively); `HAL_NVS_STATUS_NOT_FOUND` ->
  `kNotProvisioned` (the key was never written -- a normal, expected state
  before provisioning, not a failure); `HAL_NVS_STATUS_NO_SPACE` ->
  `kCorrupt` (the stored value exists but does not fit the caller's
  declared buffer -- a strong signal the stored size does not match the
  expected fixed schema); anything else (`HAL_NVS_STATUS_ERR`,
  `HAL_NVS_STATUS_INVALID_ARG`) -> `kUnavailable` (a storage-subsystem-
  level problem, not a content problem).
- `secure_storage_downgrade_to_corrupt_if_invalid(status, content_valid)`
  lets a caller with its own fixed-layout schema (magic number, schema
  version, checksum -- something this generic port cannot know) downgrade
  a tentative `kAvailable` to `kCorrupt` after inspecting the actual bytes;
  any non-`kAvailable` status passes through unchanged, since there was no
  content to have validated.
- `secure_storage_fail_closed_pass(status)` is `true` only for
  `kAvailable` -- the single predicate every production call site should
  gate on, directly implementing plan #10's "fail closed on any
  non-available state" sentence as one small, reusable function rather
  than leaving each call site to hand-roll the same check.
- `secure_storage_get_u32`/`get_str`/`get_blob(namespace_id, key, ...)`
  wrap `hal_nvs_get_u32`/`get_str`/`get_blob` with a namespace-ownership
  gate layered in front: each first calls Section 2.7's
  `find_owning_namespace_for_key(key)` and returns `kUnavailable`
  immediately -- without ever touching `hal_nvs` -- if the key does not
  resolve to the namespace the caller claims (including an entirely
  unknown, null or empty key). This is itself an application of plan #10's
  own fail-closed mandate: a caller passing the wrong namespace for a key
  is a programming error, not a normal missing-value case, and must not
  silently read the wrong thing.

**Tests** (`test/host/test_secure_storage_port.cpp`, bare `assert()`, 12
tests, all passing): the raw-status mapping for all four
`hal_nvs_status_t` values that matter; the downgrade helper's four
combinations; the fail-closed predicate for all four states; a real key
(`ota_dbg_req`) correctly reporting `kNotProvisioned` before any write and
`kAvailable` with the correct value after one; a real key read through the
*wrong* namespace returning `kUnavailable` with the output buffer/value
provably untouched (pre-filled with a sentinel, asserted unchanged --
proving `hal_nvs` was never called); an unknown key and a null key both
returning `kUnavailable` without touching the buffer; a string value
round-tripping to `kAvailable`; a blob value round-tripping to
`kAvailable`; a blob read through a too-small buffer correctly reporting
`kCorrupt` (exercising the `HAL_NVS_STATUS_NO_SPACE` mapping against a
real stored value, not a synthetic status); and a dynamically-built
schema-v4 reporting-profile key (`rptp_c07`) resolving through the
registry's prefix pattern and correctly reporting `kNotProvisioned` --
proving the ownership gate (and so this port) works for pattern-matched
keys, not just exact literal ones.

Built and run via `zgw-host-tools:s0`: the full repository host suite
passes **85/85** (84 pre-existing + this new test), proving the addition
is purely additive. `cppcheck --enable=warning,performance,portability`
via `zgw-host-tools:cppcheck` reports zero findings against the new
`.cpp`. `check_arch_invariants.sh` reports `high=0, medium=0, low=0`,
unchanged -- this addition introduces no new HAL contract or route
registration for that script to check.

### 2.9 `hal_security_state.h`/`.c` + `secure_storage_port`'s write gate -- runtime encryption-verified write gate (plan #12)

Plan #12 requires: *"Ensure no new production secret is written before NVS
Encryption and required key protection are verified at runtime."* This
sub-slice adds a new minimal HAL module plus a write-side extension of
Section 2.8's storage port, built directly on both the namespace registry
(Section 2.7) and the read port (Section 2.8).

**`components/app_hal/include/hal_security_state.h`/`.c`** (new, following
`hal_identity.c`'s exact `ESP_PLATFORM`-vs-host dual-path convention, with
a `hal_security_state_test.h` mock-override header mirroring
`hal_identity_test.h`'s own): `hal_security_state_flash_encryption_enabled()`
wraps ESP-IDF's real `esp_flash_encryption_enabled()`
(`bootloader_support/include/esp_flash_encrypt.h`) on `ESP_PLATFORM` --
confirmed against the real symbol inside `espressif/idf:release-v5.5`
before use, the same discipline as every other Kconfig/API symbol in this
stage (`bootloader_support` was already an `app_hal` `REQUIRES` dependency,
so no `CMakeLists.txt` dependency change was needed). That function reads
the actual eFuse-backed Flash Encryption state directly at runtime, not
the Kconfig profile that happened to be compiled in -- catching a real
drift case `verify_production_security_profile.py` cannot (a device
flashed with a production-profile build whose Flash Encryption eFuse bit
was never actually burned). Per Section 2.1's approved profile
(`CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC=y`: "NVS encryption key
sourced from the Flash Encryption key"), Flash Encryption's runtime state
*is* NVS Encryption's key-protection state for this project's specific
configuration -- checking one real signal is sufficient, not an
approximation. Host builds always return `false` (the safe fail-closed
default) unless overridden via the test-only mock, which -- like
`hal_identity_test.h`'s base-MAC override -- is a no-op on `ESP_PLATFORM`
so no build profile can spoof this security-relevant state.

**`secure_storage_port`'s write side** (extending Section 2.8's pair, not
a new file -- the read and write halves of one cohesive port):
`SecureStorageWriteResult{kWritten, kRejectedWrongNamespace,
kRejectedEncryptionNotVerified, kWriteFailed}` plus
`secure_storage_write_precondition_met(namespace_id)` (`true` unconditionally
for a namespace with `encryption_required == false`; delegates to
`hal_security_state_flash_encryption_enabled()` otherwise) and
`secure_storage_set_u32`/`set_str`/`set_blob(namespace_id, key, ...)`,
which apply the same namespace-ownership gate as the read path *and* the
encryption-verified gate, in that order, before ever calling
`hal_nvs_set_*`. A rejected write never reaches `hal_nvs` at all -- proven
directly in tests, not just asserted from the return value.

**Tests**: `test/host/test_hal_security_state.cpp` (4 assertions: default
state is `false`; the mock setter/resetter both work). `test/host/
test_secure_storage_port.cpp` extended with 6 more tests (18 total in that
file now): the write-gate predicate is unconditionally `true` for a
non-secret namespace regardless of encryption state, and correctly tracks
encryption state for a secret one; a non-secret-namespace write succeeds
regardless of encryption state; a secret-namespace write is rejected *and
confirmed unwritten* (read back via `hal_nvs_get_str` directly, asserting
`HAL_NVS_STATUS_NOT_FOUND`) while encryption is unverified; the same write
succeeds and round-trips correctly once the mock reports encryption
verified; and a wrong-namespace write is rejected with the underlying
stored bytes proven byte-for-byte unchanged (captured before/after,
compared).

**Verification**: built and run via `zgw-host-tools:s0` -- the full
repository host suite now passes **86/86** (85 pre-existing + 2 new test
executables), proving the addition is purely additive.
`cppcheck --enable=warning,performance,portability` via
`zgw-host-tools:cppcheck` reports zero findings against both new `.cpp`/
`.c` files. `check_arch_invariants.sh` reports `high=0, medium=0, low=0`,
unchanged. Additionally -- since this is the first S5 sub-slice to call a
genuinely new ESP-IDF *function* (not just a Kconfig symbol) --
`hal_security_state.c` was verified against a **full real
`idf.py -B build-hal-test set-target esp32c6 build`** inside
`espressif/idf:release-v5.5` (ordinary, non-`ZGW_PRODUCTION_BUILD` build:
this change has no Kconfig dependency): `1181/1181` steps completed, real
`esp_flash_encryption_enabled()` from `bootloader_support` linked
correctly with no missing-symbol or missing-include errors. Build
artifacts were cleaned up via `docker run ... rm -rf` afterward (root-
owned) and the `idf.py`-touched `dependencies.lock` was restored via
`git checkout` (this project's established Docker-artifact-cleanup
discipline).

Deliberately not implemented: no existing production call site
(`network_manager.cpp`, `connectivity_manager.cpp`, etc.) has been
migrated to call `secure_storage_set_str`/`get_str` yet -- they still call
`hal_nvs_set_str`/`get_str` directly, so today's live write behavior is
completely unchanged by this sub-slice. Wiring real call sites to this
port is future work, likely alongside #11's migration scaffolding (a
migrated value needs both the write gate and the restart-safe read-legacy/
validate/write-encrypted/read-back/erase-plaintext sequence together).

### 2.10 `hal_nvs_erase_key` + `secure_storage_erase` + `secure_storage_migration` -- restart-safe migration scaffolding (plan #11)

Plan #11 requires: *"Add restart-safe migration scaffolding for legacy
plaintext values: read legacy, validate, write encrypted, read back/
verify, then erase plaintext. No automatic migration executes until S6
authorization/physical-presence policy exists."* This sub-slice adds the
missing erase primitive plus a generic, fully tested migration function
built on Sections 2.7-2.9 -- and nothing calls it. There is no
`main/app_main.cpp` boot hook, no S6 authorization flow (which does not
exist yet); wiring it into a real boot/authorization path is future work,
matching every prior S5 sub-slice's "foundation only, no existing call
site touched" discipline.

**`hal_nvs_erase_key`** (new, extending `hal_nvs.h`/`.c`): this
repository's recon at the start of the "Encrypted storage foundation"
cluster (Section 3.4) found **no erase function anywhere** in the NVS
HAL. Added following `hal_nvs.c`'s exact existing per-function pattern
(`nvs_open`/operation/`nvs_commit`/`nvs_close` on `ESP_PLATFORM`, a
parallel host mock otherwise) and wrapping ESP-IDF's real
`nvs_erase_key(nvs_handle_t, const char*)` -- confirmed against the real
symbol and its documented return codes (`ESP_OK`, `ESP_ERR_NVS_NOT_FOUND`
for a key that never existed, `ESP_ERR_NVS_INVALID_HANDLE`, etc.) inside
`espressif/idf:release-v5.5` before writing any code, the same discipline
as every other new API call in this stage. Type-agnostic: the host mock
searches all three internal per-type tables (u32/str/blob) and clears
whichever one actually holds the key, since a real NVS key carries no
type tag of its own at this API level.

**`secure_storage_erase`** (extending Section 2.8/2.9's port): applies the
same namespace-ownership gate as the read/write paths, then calls
`hal_nvs_erase_key`. Deliberately idempotent --
`HAL_NVS_STATUS_NOT_FOUND` is treated as success (`kErased`), not a
failure, since a caller retrying a previously-interrupted cleanup step
(exactly what plan #11's migration needs) should never have to distinguish
"already gone" from "just removed".

**`secure_storage_migration.hpp`/`.cpp`** (new): `secure_storage_migrate_str()`
performs the plan's own six-step sequence -- check destination first
(restart-safety), read source, validate, write destination (through
Section 2.9's encryption-verified gate), read back and verify, only then
erase the source -- entirely through Sections 2.8/2.9/2.10's primitives,
so their guarantees (namespace ownership, fail-closed encryption gate,
idempotent erase) apply automatically without this file re-implementing
them. Matches the "Error, transaction and concurrency behavior" contract
text directly: *"Interrupted encrypted migration remains restart-safe and
never deletes plaintext before encrypted readback succeeds."*

**A real design flaw found and fixed while writing this sub-slice's own
tests, not by a separate review pass**: an earlier draft allowed the
source and destination to name the literal same namespace+key, reasoning
that this project's whole-partition NVS Encryption (Section 2.1,
`CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC=y`) is transparent below
`hal_nvs`'s API, so a real migration might simply rewrite a value in
place. Attempting to write a test for that case surfaced the actual
problem directly: the restart-safety check ("does the destination already
hold a valid value?") cannot distinguish *"a prior call already migrated
this"* from *"the legacy plaintext value is simply sitting there, never
migrated at all"* when source and destination are the same slot -- so
plan #12's encryption-verified write gate would be silently, permanently
bypassed for any value that already exists by the time this function is
ever called. `secure_storage_migrate_str()` now rejects a same-namespace-
and-key call outright (`kInvalidSameSourceAndDestination`) before touching
storage at all, and the header documents why. This is the same kind of
real defect this stage's discipline keeps catching by actually building
and testing rather than reviewing intent alone (Section 2.1's JTAG symbol
mistake, Section 2.4's partition-offset blocker, etc.) -- caught and fixed
within this same pass, not shipped and found later.

**Tests** (`test/host/test_secure_storage_port.cpp` extended with 3 erase
tests; new `test/host/test_secure_storage_migration.cpp` with 7 tests, all
using real registered keys from Section 2.7's registry as generic string
slots -- e.g. `kOperationJournalDiagnostics`'s `ota_dbg_req`/`ota_dbg_step`,
`kCoreDeviceState`'s `dstate_a`/`dstate_b`, `kWifiCredentials`'s
`wifi_ssid`/`wifi_password` -- matching how other host tests in this
repository already reuse real keys as generic test slots): no-legacy-value
is a no-op; a fresh migration moves the value and erases the source;
restart-safety (destination pre-populated, source still present) performs
cleanup-only without re-writing the destination; a rejecting validator
blocks the write and leaves both source and destination untouched; a
secret-namespace migration is rejected (source untouched) while encryption
is unverified and then succeeds once verified (continuing from the exact
same state, proving the retry path); and the same-source-and-destination
rejection fires immediately.

**Verification**: built and run via `zgw-host-tools:s0` -- the full
repository host suite now passes **87/87** (86 pre-existing + this
sub-slice's new test executable and extended assertions), proving the
addition is purely additive. `cppcheck --enable=warning,performance,portability`
via `zgw-host-tools:cppcheck` reports zero findings against all three
changed/new files (`hal_nvs.c`, `secure_storage_port.cpp`,
`secure_storage_migration.cpp`). `check_arch_invariants.sh` reports
`high=0, medium=0, low=0`, unchanged. Since `hal_nvs_erase_key` calls a
real new ESP-IDF function, also verified via a full real
`idf.py -B build-migration-test set-target esp32c6 build` inside
`espressif/idf:release-v5.5` (ordinary, non-`ZGW_PRODUCTION_BUILD` build):
`1182/1182` steps completed (one step more than Section 2.9's prior real
build, from the added source file), `nvs_erase_key` linked correctly.
Build artifacts cleaned up and `dependencies.lock` restored via
`git checkout` afterward, per this project's established discipline.

Deliberately not implemented: no production call site migrates anything
yet, and the namespace-routing question Section 2.7 already flagged as
open (real per-category ESP-IDF namespaces vs. today's single shared one)
remains open -- this sub-slice's same-key rejection means a real migration
needs a genuinely distinct destination key at minimum before it can be
wired to any real credential, which is exactly the decision Section 2.7
already deferred, now confirmed to matter concretely rather than just in
the abstract.

### 2.11 `tls_provisioning_storage_port.hpp`/`.cpp` -- TLS/provisioning key storage interfaces (plan #13)

Plan #13 requires: *"Add production TLS/provisioning key storage
interfaces consumed by S6; this stage does not generate untracked
production certificates or shared secrets."* This sub-slice defines
exactly that interface -- thin, typed specializations of Section 2.8/2.9's
generic `secure_storage_get_blob`/`set_blob`, scoped to concrete key names
now added to the Section 2.7 registry's `kTlsIdentity` and
`kManufacturingProvisioning` entries (both were `implemented_today = false`
with zero key patterns before this sub-slice; both now have real key
patterns and a real, tested code path that can read/write them, so both
flip to `implemented_today = true` -- matching the registry's own stated
bar: "true = real `hal_nvs_*` call sites exist today", not "S6 wires this
up end-to-end"). No new read/write/erase mechanism is introduced -- every
guarantee Sections 2.7-2.9 already provide (namespace ownership, the plan
#12 encryption-verified write gate) applies automatically.

**Scope, matching the plan's own stage split**: S6's "authenticated
current/next certificate rotation adapter with atomic active-slot
selection" (S6's own `Required changes` text) is explicitly out of scope
here -- this sub-slice defines storage primitives only: which slot is
"active", atomic activation/rollback, and certificate
parsing/SAN/issuer/expiry validation are all S6's job. **Nothing in this
repository calls any function this sub-slice adds** -- S6 is the actual
consumer, per the plan text itself. And per the plan's explicit
constraint, **this sub-slice generates, reads and writes no real
certificate, private key or provisioning secret** -- every byte pattern in
its own tests is synthetic placeholder data (`0xA1`/`0xB2`-style filler),
never anything resembling production key material.

**New key patterns** added to the registry:

| Namespace | Keys | Purpose |
|---|---|---|
| `kTlsIdentity` | `tls_key_cur`, `tls_key_nxt` | Device management TLS private key, current/next slot (FD-17 rotation) |
| `kTlsIdentity` | `tls_cert_cur`, `tls_cert_nxt` | Device management TLS certificate, current/next slot |
| `kTlsIdentity` | `tls_ca` | Product CA/trust anchor -- a single slot, not rotated the way the device's own certificate is (FD-21 lists it as its own separate Preserve item) |
| `kManufacturingProvisioning` | `mfg_pop` | Manufacturing proof-of-possession |
| `kManufacturingProvisioning` | `mfg_efuse_rec` | The eFuse provisioning-template record (`scripts/efuse_provisioning_template.py`, plan #5's schema, serialized) |

Both namespaces remain `PRESERVE_ON_FACTORY_RESET` (Section 2.7,
unchanged) -- **this module deliberately exposes no erase function**: a
convenience erase here would invite exactly the mistake plan #16/#17's
erase enforcement must avoid (broad erase touching preserved trust/
identity material).

**Tests** (`test/host/test_tls_provisioning_storage_port.cpp`, 5 tests,
all passing): a fresh manufacturing record correctly reports
`kNotProvisioned` before any write; private-key current/next slots
round-trip independently (writing one never touches the other, checked by
comparing the two round-tripped values directly, not just their return
codes); a certificate write is rejected -- and confirmed not to reach
storage -- while encryption is unverified (the same #12 dependency Section
2.9/2.10's tests already exercise, now proven for this interface too); the
product CA round-trips once encryption is verified; and the manufacturing
PoP and eFuse-record slots are proven independent of each other.

**Verification**: built and run via `zgw-host-tools:s0` -- the full
repository host suite now passes **88/88** (87 pre-existing + this
sub-slice's new test executable), proving the addition is purely
additive. `cppcheck --enable=warning,performance,portability` via
`zgw-host-tools:cppcheck` reports zero findings against both changed
files (`nvs_namespace_registry.cpp`'s extended entries,
`tls_provisioning_storage_port.cpp`). `check_arch_invariants.sh` reports
`high=0, medium=0, low=0`, unchanged. Also verified against a full real
`idf.py -B build-tls-test set-target esp32c6 build` inside
`espressif/idf:release-v5.5`: `1183/1183` steps completed (one more than
Section 2.10's prior real build, from the added source file) -- this
module calls no new ESP-IDF API of its own (only the already-verified
`secure_storage_get_blob`/`set_blob`), so this build mainly confirms the
new translation unit compiles cleanly under the real target toolchain,
not a new API surface. Build artifacts cleaned up and `dependencies.lock`
restored via `git checkout` afterward.

With this sub-slice, **plan items #9-#13 and #15 (six of S5's ten
"Encrypted storage foundation" items) are now complete** -- everything
except #14 (redaction), #16/#17 (erase/preserve enforcement) and #18 (the
reset-journal storage port).

### 2.12 Redaction: logs, crash reports and completion evidence (plan #14)

Plan #14 requires: *"Redact all key/credential material from logs, crash
reports and completion evidence."* This is the first S5 sub-slice with a
requirement split across three genuinely different mechanisms rather than
one storage primitive, so it was treated as three separate, concrete
pieces of work -- each grounded in something real, not a single generic
"redaction system."

#### 2.12.1 Logs

**`secure_log_redaction.hpp`/`.cpp`** (new): `redact_value_for_log(value,
value_len)` reads *only* `value_len`, never `value`'s content -- it is
structurally impossible for a raw secret byte to reach the formatted
output, because the function never looks at the bytes at all. This is a
deliberately stronger form of redaction than
`scripts/efuse_provisioning_template.py`'s `redact_key_material()` (plan
#5/#8), which keeps a SHA-256 digest of key material because provisioning
evidence has a real, narrow need to answer "is this the same key as a
prior record" without storing the key -- a log line has no such need, so
even a digest is left out here, removing an entire class of "is this hash
actually safe" review question for every future call site.
`format_redacted_value_summary()` renders this as a fixed string, e.g.
`[REDACTED 11 bytes]`.

Wired into **`secure_storage_port.cpp`**, which previously had *no*
logging at all (Sections 2.8-2.10 added zero diagnostic output): every
read/write/erase now logs the key name, namespace outcome and (for writes)
a redacted value summary, `ESP_PLATFORM`-only via a new `SS_LOGI`/`SS_LOGW`
guarded-macro pair (a no-op on host builds, matching this repository's
established `CM_LOGI`-style pattern from `connectivity_manager.cpp` and
others) tagged `LOG_TAG_SECURE_STORAGE` (new, added to `log_tags.h`). Key
*names* and namespace IDs are logged directly -- they are not secret --
matching `hal_nvs.c`'s own existing convention.

**Audit of pre-existing log statements**, not just new code: every
`ESP_LOG*` call already present in every S5-added/touched file
(`hal_nvs.c`, `hal_security_state.c`, `secure_storage_migration.cpp`,
`tls_provisioning_storage_port.cpp`, `nvs_namespace_registry.cpp`) was
grepped and inspected. Result: `hal_nvs.c`'s existing 18 log statements
(predating S5) already only ever print the key *name*, byte lengths and
ESP-IDF error strings (`esp_err_to_name`) -- never a stored value. No
change was needed there; this is a real, checked finding, not an assumed
one. The other five files had zero log statements before this sub-slice.

#### 2.12.2 Crash reports

Grepped ESP-IDF's real `espcoredump` Kconfig
(`components/espcoredump/Kconfig`) inside `espressif/idf:release-v5.5`
rather than assuming: **`CONFIG_ESP_COREDUMP_CAPTURE_DRAM`**'s own help
text reads *"Include whole .bss and .data sections and heap data into
core dump file"* -- exactly the memory regions where this project's
secret-namespace values (Wi-Fi credentials today, TLS keys/manufacturing
PoP once Sections 2.9-2.11's storage is actually populated) live at
runtime. Added to `scripts/verify_production_security_profile.py`'s
`FORBIDDEN_SYMBOLS` (plan #3's verifier), so a production build with this
symbol enabled fails the same way `SECURE_BOOT_ALLOW_JTAG`/
`SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT` already do.

**A real, honest finding, not a gap closed by this addition**: this
symbol's own ESP-IDF default is already `n` (its parent choice,
`ESP_COREDUMP_TO_FLASH_OR_UART`, itself defaults to `ESP_COREDUMP_ENABLE_TO_NONE`
-- coredump is fully *disabled* by default), and this repository's
`sdkconfig.defaults*`/`sdkconfig.production.esp32c6` set nothing related
to coredump at all -- confirmed by grepping all three files, zero matches.
So no crash report is currently being generated at all in any build this
repository produces, and this new forbidden-symbol check exists to catch
a *future* accidental opt-in (e.g. someone enabling coredump-to-flash for
debugging and reaching for `CAPTURE_DRAM` too, not realizing the
redaction implication), not a currently-active leak. Worth naming
directly: a **`coredump` partition already exists** in both
`partitions.csv` and `partitions.production.csv` (`0x534000`, 64 KB,
predating S5 entirely), suggesting coredump-to-flash was intended to be
enabled at some point -- whether/when to actually turn it on is an
unrelated product decision this sub-slice does not make; the forbidden-
symbol check is correct regardless of that decision's outcome.

**Verification**: `scripts/test_verify_production_security_profile.py`
gained a new fixture-based test
(`test_coredump_dram_capture_forbidden`). Re-ran a full real
`ZGW_PRODUCTION_BUILD=1 idf.py build` inside `espressif/idf:release-v5.5`
and checked the **real generated `sdkconfig`** directly: grepping for
`ESP_COREDUMP_CAPTURE_DRAM` found no match at all (the whole coredump
Kconfig submenu is absent from the generated file since the feature is
disabled), and `scripts/verify_production_security_profile.py sdkconfig`
against that real file reported `OK` -- confirming the new forbidden-
symbol check does not false-positive against this project's actual
approved profile.

#### 2.12.3 Completion evidence

Audited this repository's own `implementation-evidence/*.json` files and
`docs/security/*.md` directly: `grep`-searched for PEM markers (`BEGIN
CERTIFICATE`/`BEGIN PRIVATE KEY`/`BEGIN RSA`/`BEGIN EC PRIVATE`), for
password-shaped key/value patterns, and for the specific synthetic test
values used across this stage's own test files (e.g. `hunter2`,
`MyNetwork`) to confirm none had leaked from source into evidence or
documentation. All three searches returned zero matches. This is a real,
executed check on this stage's own artifacts, not an assumption --
consistent with every test file across Sections 2.7-2.11 having already
used only synthetic/placeholder values (`0xA1`/`0xB2`-style filler bytes,
never anything resembling real credential material), which is exactly why
the audit came back clean rather than needing a fix.

#### 2.12.4 Tests and verification

`test/host/test_secure_log_redaction.cpp` (new, 7 tests): the summary
carries only length, is unaffected by a null pointer with a stale nonzero
length argument, formats predictably, rejects a null/zero-capacity output
buffer, and truncates safely (never overruns) into an undersized buffer.
Full repository host suite: **89/89** passing (88 pre-existing + this
sub-slice's new test executable) via `zgw-host-tools:s0`. `cppcheck
--enable=warning,performance,portability` via `zgw-host-tools:cppcheck`
reports zero findings against both changed C++ files. `check_arch_invariants.sh`
reports `high=0, medium=0, low=0`, unchanged. Python: 50/50 total across
`scripts/test_verify_production_security_profile.py` (32, +1 new) and
`scripts/test_efuse_provisioning_template.py` (18, unchanged).

### 2.13 `factory_reset_namespace_erase.hpp`/`.cpp` -- typed namespace erase enforcement (plan #16/#17)

Plan #16: *"Preserve eFuse/security state, flash/NVS encryption key
material, factory GatewayId source, manufacturing PoP, product CA/trust
anchors and encrypted device TLS current/next slots."* Plan #17: *"Mark
admin/session, Wi-Fi, MQTT, Zigbee network/device/reporting, Matter,
operation journal and legacy migration/tombstone data for erase. Provide
typed namespace erase operations; do not use broad whole-partition erase
that can touch preserved material."*

Plan #16's classification itself was already complete (Section 2.7, plan
#15) -- every namespace already carries exactly one
`NvsResetClassification`. What this sub-slice adds is the *enforcement*:
`erase_namespace(namespace_id)` refuses outright -- before touching any
key at all -- any namespace whose registry classification is not
`kEraseOnFactoryReset`. This is the plan's own "do not use broad
whole-partition erase that can touch preserved material" made structural
rather than a convention someone has to remember: a `PRESERVE_ON_FACTORY_RESET`
or `RESET_JOURNAL_ONLY` namespace cannot reach `hal_nvs_erase_key` through
this function no matter what a caller passes.

**The prefix-key limitation, named directly rather than hidden**:
`erase_namespace()` only erases EXACT key patterns. `kZigbeeNetworkDeviceReporting`
is the one namespace with prefix patterns (`rptp_`/`cfg_rpt_`, config_manager.cpp's
dynamically-built per-reporting-profile keys) -- the registry only stores
the prefix string, not the real bounded keyspace a specific owning module
builds from it. A second function, `erase_namespace_key_range(namespace_id,
prefix, suffix_chars, suffix_char_count, index_count)`, handles this: it
builds every `<prefix><suffix_char><2-digit index>` key in the given
bounded range (matching `config_manager.cpp`'s real
`build_profile_nvs_key`/`build_legacy_v2_profile_nvs_key` format exactly)
and erases each. Same refusal guarantee as `erase_namespace()`. Tested
against the *real* production shape: `"rptp_"` with suffix characters
`"dcirk"` (5, covering schema v4's d/c/i/r plus the v3 legacy `k` check)
and `index_count=16` (matching `ConfigManager::kMaxReportingProfiles`),
and `"cfg_rpt_"` with `"k"` and `16` (schema v2 legacy) -- not a
hypothetical shape.

**A real bug found only by the real target build, not the host build**:
the first version of `erase_namespace_key_range()` formatted the dynamic
key index with a bare `%02u` against a `uint32_t` argument. This compiles
and passes cleanly on the x86-64 host toolchain (where `uint32_t` is
`unsigned int`, exactly matching `%u`), but the real ESP32-C6 riscv32
target toolchain resolves `uint32_t` to `unsigned long` -- a real,
target-specific format-specifier mismatch `-Werror` caught only when
compiling for real hardware. Fixed with an explicit
`static_cast<unsigned>(index)`, matching `hal_nvs.c`'s own established
`(unsigned)`-cast convention for exactly this reason. This is the same
category of "only a real target build catches it" defect this stage has
found repeatedly (Section 2.1's JTAG symbol, Section 2.4's
partition-offset blocker) -- caught and fixed within this same pass, not
shipped.

**Tests** (`test/host/test_factory_reset_namespace_erase.cpp`, 7 tests):
a `PRESERVE`-classified namespace (`kTlsIdentity`) is refused and its data
proven untouched; `kResetJournal` (`RESET_JOURNAL_ONLY`) is refused the
same way -- the first concrete proof of plan #18's "survives erasing"
property; an `ERASE`-classified namespace's exact keys are actually
erased; a namespace with both exact and prefix keys has only its exact
keys cleared by `erase_namespace()` (documenting the limitation
directly); `erase_namespace_key_range()` clears the real
`rptp_`/reporting-profile shape; the range function refuses a
`PRESERVE`-classified namespace exactly like the whole-namespace one;
null `prefix`/`suffix_chars` arguments are rejected defensively.

Deliberately scaffolding, matching every S5 sub-slice's discipline:
nothing in this repository calls either function -- there is no real
factory-reset flow yet (S8's job, built on top of this and Section 2.10's
migration scaffolding).

### 2.14 `reset_journal_storage_port.hpp`/`.cpp` -- protected reset-journal storage port (plan #18)

Plan #18: *"Add a dedicated protected reset-journal storage port that
survives erasing user/application namespaces and can atomically represent
the four FD-21 states."*

**"Survives erasing"** is proven, not assumed: `kResetJournal`'s registry
classification (`RESET_JOURNAL_ONLY`, Section 2.7) means Section 2.13's
`erase_namespace()` refuses it outright by construction -- a dedicated
test (`test_reset_journal_survives_an_erase_namespace_attempt`) writes a
real state, calls `erase_namespace(kResetJournal)`, confirms the refusal,
and confirms the state is still exactly what it was, exercising both
sub-slices together end-to-end.

**"Atomically represent the four states"**: the journal is one `u32` key
(new: `"reset_journal"`, added to `kResetJournal`'s registry entry,
`implemented_today` flipped to `true`), written in a single
`hal_nvs_set_u32` call. This inherits its atomicity from ESP-IDF's own
NVS commit semantics rather than reimplementing anything -- a single
key's value is never observed torn/partial across a power loss, matching
FD-22's "prefer ESP-IDF-provided ... platform lifecycle mechanisms"
principle directly. `get_reset_journal_state()`/`set_reset_journal_state()`
are thin, typed wrappers over Section 2.8/2.9's `secure_storage_get_u32`/
`set_u32`, reusing `SecureStorageStatus` directly for reads (so
`kNotProvisioned` before any reset has ever been requested is exactly as
meaningful here as for any other namespace) and a small
`ResetJournalWriteResult` for writes (adding `kRejectedInvalidState` for
an out-of-range enum value, distinct from a storage-layer failure).
`kResetJournal` has `encryption_required == false` (Section 2.7 -- the
journal is a small state enum, not secret material), so no plan #12
encryption gate applies here.

**Tests** (`test/host/test_reset_journal_storage_port.cpp`, 5 tests):
not-provisioned before any write; every one of FD-21's four states
(`kRequested`/`kErasing`/`kReinitialized`/`kCommissioningReady`)
round-trips independently; an out-of-range enum value is rejected before
writing; a raw out-of-range value written directly (simulating external
corruption) is read back as `kCorrupt`; and the cross-module
"survives erasing" proof described above.

Deliberately scaffolding: nothing in this repository calls
`set_reset_journal_state()` -- the real reset flow that would drive this
port through its actual states is S8's job.

**Verification for both Sections 2.13/2.14 together**: built and run via
`zgw-host-tools:s0` -- the full repository host suite now passes
**91/91** (89 pre-existing + 2 new test executables), proving both
additions are purely additive. `cppcheck --enable=warning,performance,portability`
via `zgw-host-tools:cppcheck` reports zero findings against all three
changed/new files (`nvs_namespace_registry.cpp`'s extended `kResetJournal`
entry, `factory_reset_namespace_erase.cpp`, `reset_journal_storage_port.cpp`).
`check_arch_invariants.sh` reports `high=0, medium=0, low=0`, unchanged.
Verified against a full real `idf.py -B build-erase-test set-target
esp32c6 build` inside `espressif/idf:release-v5.5` -- this is where the
`uint32_t`/`%02u` format-specifier bug above was actually found and then
confirmed fixed (a second, clean full build after the fix).

**With Sections 2.13/2.14, all ten of plan S5's "Encrypted storage
foundation" items (#9-#18) are now complete.**

## 3. What is explicitly deferred

### 3.1 (Resolved) ESP-IDF version check in the verifier -- plan #4, second half

Previously deferred; now implemented. `verify_production_security_profile.py
--project-description PATH` checks the resolved ESP-IDF version (Section
2.3/2.3.1) -- the user decided the open policy question (exact 5.5.2 pin
vs. tracking the release branch) in favor of allowing any `5.5.x` patch,
matching this project's actual `release-v5.5`-tracking CI configuration.
Plan #4 is now fully implemented (Kconfig-symbol half from sub-slice 1 +
version-check half from this sub-slice). One related item remains
genuinely open, not part of plan #4 itself: this repository's git-tracked
`dependencies.lock` independently pins `idf: version: 5.5.2` for the ESP-IDF
Component Manager (Section 2.3) -- a real build wants to rewrite that line
to `5.5.5`, but updating a git-tracked lock file is a change with its own
review implications and was left untouched in this sandbox pass (any
in-sandbox `idf.py reconfigure`/`build` run that touches it was reverted
via `git checkout -- dependencies.lock` before finishing, per this
project's established Docker-artifact-cleanup discipline).

### 3.2 Everything requiring real ESP32-C6 hardware or a manufacturing/eFuse environment

- Actual Secure Boot v2 signing-key generation/management, the two-phase
  dry-run/burn eFuse provisioning workflow, device quarantine for
  unexpected eFuse state, and post-burn verification (plan #6-#8) --
  `BLOCKED_SECURITY_PROVISIONING`, since this sandbox has no
  manufacturing/eFuse environment to dry-run against, exactly the
  condition the plan's own preconditions name. (Plan #5, the
  provisioning template/schema itself, is done -- Section 2.6 -- since it
  is a pure data-transformation format with no hardware interaction; #6-8
  are what actually need real silicon.)
- Every target/HIL test from the plan's own list (Secure Boot v2
  readback, Flash Encryption release-mode readback, NVS Encryption
  round-trip and raw-flash non-disclosure check, JTAG/UART/download
  policy verification on real silicon, reboot/power-loss during encrypted
  migration, unexpected eFuse state causing quarantine) -- these need a
  physical ESP32-C6, which this sandbox does not have, even though ESP-IDF
  itself (the software toolchain) turned out to be available via Docker.
- A CI job that builds the production profile through the ESP-IDF
  container on every push/PR (analogous to the existing `firmware-build`
  job, but with `ZGW_PRODUCTION_BUILD=1` set and the verifier run against
  the real output) -- now that Section 2.2/2.4 prove a full production
  build actually succeeds end-to-end, this is a much more concrete,
  lower-risk follow-up than it was before this sub-slice's real-build
  pass; not added yet only because CI changes deserve their own deliberate
  pass, not a rushed addition.

### 3.3 Remaining plan items not yet started

- **eFuse and manufacturing foundation, remainder** (plan #6-#8): the
  two-phase dry-run/burn workflow, device quarantine for unexpected eFuse
  state, and burn-time redacted-evidence enforcement --
  `BLOCKED_SECURITY_PROVISIONING` (Section 3.2). (Plan #5, the
  machine-readable template/schema itself, is done -- Section 2.6.)
- **Encrypted storage foundation**: fully complete. All ten required
  changes (#9-#18) are done -- Sections 2.7-2.14. Nothing in this cluster
  remains unstarted; what remains for the *stage as a whole* is exclusively
  #6-#8 above (hardware/manufacturing-blocked) and the items named in
  Section 3.1's "still open" note (the `dependencies.lock` 5.5.2 pin) and
  Section 3.2's last bullet (a CI job that builds the production profile).
  Every S5 deliverable that does not require real ESP32-C6 hardware or a
  manufacturing/eFuse environment has now been implemented and tested --
  see Section 5 for the stage-level closing summary.

### 3.4 Existing NVS state this stage's later sub-slices will need to account for

Recon performed before Section 2.7's sub-slice; Section 2.10 has since
converted the "no erase function anywhere" gap into a real, tested
primitive:

- The current NVS HAL (`components/app_hal/include/hal_nvs.h`) still has
  **no per-namespace concept at all** -- every call goes through one
  hardcoded ESP-IDF namespace `"zigbee_gateway"`. Section 2.7's registry
  records the *conceptual* ownership/classification split
  (`nvs_namespace_registry.hpp`'s `NvsNamespaceEntry::nvs_namespace`);
  Section 2.8/2.9's storage port adds typed, fail-closed read/write
  results on top of it; Section 2.10 adds the erase primitive
  (`hal_nvs_erase_key`/`secure_storage_erase`) and migration scaffolding;
  Section 2.13 adds typed namespace-erase enforcement on top of all of
  that. None of these route to a real distinct per-category ESP-IDF
  namespace yet -- that routing question is still open (Section 2.10's
  own same-key-rejection finding confirms it now matters concretely for
  any real migration, not just in the abstract) -- but every S5-scoped
  software deliverable that does not depend on resolving it is now
  complete; the routing question itself is a named, unresolved item
  (Section 5) rather than a blocker on anything S5 required.
- Exactly two plaintext credential keys exist in NVS today: `wifi_ssid`
  and `wifi_password` (`NvsNamespaceId::kWifiCredentials` in the registry).
  MQTT credentials are still not stored in NVS at all -- compile-time
  `sdkconfig` values only, with no default set in either
  `sdkconfig.defaults*` file today (`NvsNamespaceId::kMqttCredentials`,
  `implemented_today = false`).
- `hal_identity.h` still exposes exactly one function
  (`hal_identity_get_factory_base_mac`) -- no eFuse-state accessor exists
  yet; a future eFuse-policy sub-slice will extend this file following its
  existing `int ...; 0 on success, negative on failure` /
  `ESP_PLATFORM`-vs-host dual-path convention. Section 2.7's registry
  notes explicitly that the factory GatewayId this function derives is
  never written to NVS at all, so it has no namespace entry of its own.

## 4. Environment note

Verified via `python3` directly (stdlib `unittest`) for the verifier
script's own logic, and via the real `espressif/idf:release-v5.5` Docker
image (pulled during this sub-slice; the same tag this project's CI
already depends on) for the Kconfig symbol/build verification in Section
2 -- this is a genuine change from earlier stages' assumption that
ESP-IDF was unavailable in this sandbox; it is available via Docker,
matching how the project's own CI already runs it. A full
`ZGW_PRODUCTION_BUILD=1 idf.py build` succeeds end-to-end from this
sandbox (Section 2.4). The eFuse provisioning template (Section 2.6) was
grounded the same way, via `espefuse.py --virt`'s real eFuse emulation --
software-only, no real chip needed. The ESP-IDF-version check (Section
2.3.1) was grounded the same way too, via a real `idf.py reconfigure`'s
generated `project_description.json` -- also software-only. What remains
`BLOCKED_SECURITY_PROVISIONING` is specifically real-hardware/eFuse
verification (Section 3.2) -- actually flashing, signing and booting a
real device, the two-phase dry-run/burn workflow, and everything that can
only be observed on real silicon -- not the software build, the
provisioning template format, or the version-check tooling itself. The NVS
namespace registry (Section 2.7) and the typed secure-storage read port
built on top of it (Section 2.8) needed no hardware or ESP-IDF container
at all -- both are pure C++ data plus logic, verified via `zgw-host-tools:s0`
(build + this repository's full 89-test `ctest` suite) and
`zgw-host-tools:cppcheck` (zero findings), the same Docker images used for
every C/C++-touching stage since S0. Section 2.9's write gate and Section
2.10's erase primitive/migration scaffolding are likewise pure
host-testable C/C++ logic, but since each calls a genuinely new real
ESP-IDF *function* for the first time it is used
(`esp_flash_encryption_enabled()` in Section 2.9, `nvs_erase_key()` in
Section 2.10 -- not just a Kconfig symbol), both were additionally
verified against a full real `idf.py build` inside
`espressif/idf:release-v5.5` -- the same real-toolchain-grounding
discipline applied throughout this stage since the user's original
correction that ESP-IDF is available via Docker. Section 2.11's TLS/
provisioning storage interfaces call no new ESP-IDF API of their own (only
the already-verified `secure_storage_get_blob`/`set_blob`), but were still
built against the real target once, since it is a new component source
file, not just to prove a new symbol links. Section 2.12's redaction work
was verified three separate ways matching its three separate targets: the
logs clause via `zgw-host-tools:s0`'s host tests (pure logic, no hardware);
the crash-reports clause via a real `ZGW_PRODUCTION_BUILD=1 idf.py build`
inside `espressif/idf:release-v5.5` followed by running
`verify_production_security_profile.py` directly against the real
generated `sdkconfig`; and the completion-evidence clause via a direct
`grep` audit of this repository's own `implementation-evidence/*.json`
and `docs/security/*.md` files -- no hardware needed for any of the three.
Sections 2.13/2.14's erase enforcement and reset-journal port needed a
real target build too, and it earned its keep: the
`uint32_t`/`%02u`-vs-`unsigned long` format-specifier bug (Section 2.13)
compiled and passed cleanly on the x86-64 host toolchain and was caught
only once compiled for the real riscv32 ESP32-C6 target -- a concrete
demonstration of why this stage keeps re-verifying against real ESP-IDF
rather than trusting the host build alone, all the way through its final
sub-slice.

## 5. Stage-level closing summary

All ten of plan S5's "Encrypted storage foundation" required changes
(#9-#18) are now implemented and tested, alongside #1-#5's "Immutable
production security profile"/"eFuse and manufacturing foundation" items
and #8's applicable (non-hardware) half. Every S5 required change that
does not need real ESP32-C6 hardware or a manufacturing/eFuse environment
is complete:

| # | Required change | Status |
|---|---|---|
| 1 | Production sdkconfig profile (Secure Boot v2/Flash Encryption/NVS Encryption/rollback) | Done -- Section 2.1 |
| 2 | Development/production profile separation | Done -- Section 2.2 |
| 3 | `verify_production_security_profile.py` | Done -- Section 2.5 |
| 4 | Production build fails on profile/version mismatch | Done -- Sections 2.2-2.4, 2.3.1 |
| 5 | Machine-readable eFuse provisioning template | Done -- Section 2.6 |
| 6 | Two-phase dry-run/burn eFuse provisioning | `BLOCKED_SECURITY_PROVISIONING` -- Section 3.2 |
| 7 | Device quarantine for unexpected eFuse state | `BLOCKED_SECURITY_PROVISIONING` -- Section 3.2 |
| 8 | Redacted evidence only; no private keys in repo | Partial -- signing-key/insecure-escape-hatch handling (Section 2.1) and template-format redaction (Section 2.6) done; burn-time enforcement is `BLOCKED_SECURITY_PROVISIONING` |
| 9 | Explicit encrypted NVS namespace ownership | Done -- Section 2.7 |
| 10 | Typed fail-closed storage-read results | Done -- Section 2.8 |
| 11 | Restart-safe legacy-plaintext migration scaffolding | Done -- Section 2.10 |
| 12 | Runtime encryption-verified write gate | Done -- Section 2.9 |
| 13 | TLS/provisioning storage interfaces | Done -- Section 2.11 |
| 14 | Redact logs/crash reports/completion evidence | Done -- Section 2.12 |
| 15 | FD-21 preserve/erase/reset-journal classification | Done -- Section 2.7 |
| 16 | Preserve eFuse/TLS/CA/PoP/GatewayId material | Done (classification: Section 2.7; enforcement: Section 2.13) |
| 17 | Typed namespace erase, never broad whole-partition erase | Done -- Section 2.13 |
| 18 | Protected reset-journal storage port | Done -- Section 2.14 |

**What this stage did NOT do**, by design, matching its own "foundation
only" discipline applied consistently across all eleven sub-slices: no
production call site (`network_manager.cpp`, `connectivity_manager.cpp`,
`config_manager.cpp`, `matter_endpoint_registry.cpp`, etc.) has been
migrated to call any of Sections 2.7-2.14's new ports. Every existing
plaintext read/write (`wifi_ssid`/`wifi_password` included) still goes
through `hal_nvs_*` directly, completely unchanged from before S5 began.
Nothing in this repository calls `secure_storage_migrate_str()`,
`erase_namespace()`, `erase_namespace_key_range()`, or
`set_reset_journal_state()`. This is intentional: wiring real call sites,
and building the actual S6 authorization/physical-presence policy and S8
factory-reset flow that would drive these ports, is explicitly out of
scope for S5 (per the plan's own S6/S8 stage boundaries and #11's literal
"no automatic migration executes until S6 authorization... policy
exists" text) -- S5's job was to prove the foundation is real,
correct and tested, not to flip the switch on it.

**Named, unresolved items carried forward** (not blockers on any S5
required change, but real open threads a future stage should pick up):

- Real per-category ESP-IDF NVS namespace routing (`hal_nvs.c` still
  hardcodes a single `"zigbee_gateway"` namespace for every key) --
  Section 2.10's same-key-migration-rejection finding confirms this
  matters concretely once any real migration is attempted, not just in
  the abstract.
- `dependencies.lock`'s own `idf: version: 5.5.2` pin disagrees with the
  real toolchain (`5.5.5`) this project's CI actually uses -- found in
  Section 2.3.1's sub-slice, deliberately not edited.
- A CI job that builds the production profile through the real ESP-IDF
  container on every push/PR does not exist yet (Section 3.2's last
  bullet) -- now much lower-risk to add, since this stage repeatedly
  proved a full production build succeeds end-to-end.
- Whether/when to actually enable coredump-to-flash (Section 2.12.2) --
  the partition is reserved but nothing turns the feature on.
- The concrete key names this stage invented for
  `kTlsIdentity`/`kManufacturingProvisioning` (Section 2.11) are not
  derived from any real S6 design and may need to change once one exists.

**Every real defect this stage found was found by actually building and
testing, not by review of intent alone** -- a pattern that held from the
very first sub-slice to the very last: `CONFIG_ESP32C6_DISABLE_JTAG`
(fictional symbol, Section 2.1), the `CMakeLists.txt` `SDKCONFIG_DEFAULTS`
silent-shadow bug (Section 2.2), the bootloader-too-large-for-partition-
offset blocker (Section 2.4), the same-source-and-destination migration
design flaw (Section 2.10), and the `uint32_t`/`%02u` format-specifier
mismatch caught only by the real riscv32 target build (Section 2.13) --
five independent, real, verified-not-assumed findings across eleven
sub-slices, each fixed within the same pass it was found, none shipped
broken.
