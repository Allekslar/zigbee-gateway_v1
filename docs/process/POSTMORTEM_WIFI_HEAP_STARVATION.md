<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 Alex.K. -->

# Post-mortem: Wi-Fi association failure caused by `.bss` heap starvation

**Date root-caused and fixed:** 2026-08-16
**Hardware:** ESP32-C6 (QFN40) rev v0.2, factory base MAC `98:a3:16:9f:00:20`, Flash Encryption active in Development mode
**Affected profile:** production (`CONFIG_ZGW_PRODUCTION_PROFILE=y`) + Flash Encryption. Latent on every profile.
**Symptom severity:** device unusable — Wi-Fi never associated, boot crash-looped 10–16 times per minute
**Tracked in this repo as:** "Bug C" during the S6 certificate-rotation HIL round

---

## 1. Summary

A Wi-Fi station that never associated, a SoftAP that crashed on startup, and a
WPA2 4-way-handshake timeout were tracked for a long time as three separate
bugs. They were one bug: **roughly 166 KB of permanently reserved `.bss`**, held
by four global objects in [`main/app_main.cpp`](../../main/app_main.cpp), left
the Wi-Fi driver with **280–476 bytes of DMA-capable heap** during association.
The driver could not allocate a buffer for its own management frames, so every
connection attempt failed before it began.

The single dominant contributor was `MqttBridge`'s publication staging queue:
**~85 KB reserved for the device's entire uptime** for a buffer that is filled
by one sync pass and drained to empty moments later.

| | Before | After |
|---|---|---|
| Wi-Fi station connections | 0 / 19 | connects, **0 disconnects** |
| Reboots per 55 s window | 10–16 | **1 boot, 0 reboots** |
| Crashes | 10–16 | **0** |
| Free heap at `app_main()` | 66 KiB | **148 KiB** |
| DMA-capable heap during association | 280–476 B | tens of KB |

---

## 2. The decoding insight that unlocked everything

ESP-IDF's Wi-Fi driver logs frame-buffer allocation failures as:

```
wifi:m f auth
wifi:m f assoc req l=252
wifi:m f beacon l=283
```

`m f` is **"malloc failed"** for that frame — not an authentication error, not
an RF error. The driver prints the unambiguous form on the SoftAP path, which
is what made the pattern recognisable:

```
wifi:alloc eb len=752 type=4 fail
wifi:m f beacon l=283
```

Because the *same* `m f <frame>` pattern appeared on the station auth path, on
the association path, and on the SoftAP beacon path, all three "different bugs"
collapsed into one cause. Coexistence, which needs its own memory, simply
consumed enough extra to push the failure one stage earlier — from the 4-way
handshake to auth.

> **Rule of thumb for this codebase:** a Wi-Fi failure that is 100 %
> deterministic and indifferent to signal, distance, router settings, RF
> calibration and toolchain version is almost certainly a *memory* problem.
> Measure free DMA-capable heap before investigating anything else.

---

## 3. Where the memory went

`idf.py size-components` plus a parse of the largest `.bss`/`.data` symbols in
`build/zigbee_gateway.map`:

| Symbol | Size | What |
|---|---:|---|
| `g_mqtt` | **101 692 B** | `MqttBridge` |
| `g_runtime` | 41 240 B | `ServiceRuntime` |
| `g_registry` | 20 624 B | `CoreRegistry` |
| `g_matter` | 6 808 B | `MatterBridge` (`.data`) |

Inside `MqttBridge`, one member dominated:

```cpp
constexpr std::size_t kMaxMqttPublicationsPerSync = kServiceMaxDevices * 3U;  // 192

struct MqttPublishedMessage {
    char topic[64];
    char payload[384];
    bool retain;
};                                        // 452 bytes

MqttPublishedMessage pending_publications_[192];   // ≈ 85 KB, always resident
```

The queue is **transient by design** — `sync_snapshot()` fills it, then
`publish_pending_publications()` drains it to empty in batches of 8 within the
same task iteration — yet it was reserved for the device's whole uptime whether
or not MQTT was ever used.

**Why the production profile made it fatal.** Flash Encryption plus
`nvs_sec_provider` roughly halve the heap available at `app_main()` start on
this hardware: **66 KiB versus 133+ KiB** on a plain development build. The Wi-Fi
driver's own initialisation then consumes ~65 KB. On the development profile the
remainder was comfortable; on the production profile it was nearly nothing.
This is why the bug looked board-specific and profile-specific when it was
neither — it was latent everywhere, and only crossed the failure threshold on
the tighter budget.

---

## 4. Why it took so long: hypotheses tested and ruled out

Each of these was investigated with real evidence and disproven. Recording them
matters as much as recording the answer — several were *plausible* and cost real
time.

| Hypothesis | How it was ruled out |
|---|---|
| Weak signal / distance | Failed identically with the board next to the router; final logs showed RSSI −59 dBm, SNR 38 — excellent |
| Router MAC filtering | FRITZ!Box "Restrict Access to Wi-Fi" was set to *Allow all new wireless devices*; the board was listed as a known device with a prior DHCP lease |
| Damaged / loose antenna | Ruled out by the hardware owner; also inconsistent with a perfectly deterministic failure |
| Corrupted `phy_init` RF calibration | A flash erase earlier in the investigation had wiped the `phy_init` partition. Re-erasing it forced a fresh full recalibration — **no change**, and the failure stayed byte-identical across 11 consecutive boots. Zero variance argues *against* marginal calibration, which would fail intermittently |
| ESP-IDF 5.5.2 → 5.5.5 driver regression | An earlier finding claimed 5.5.2 connected reliably. Retested with current code on **both** toolchains: both failed identically (0/19 and 0/16). The earlier result was confounded — most plausibly by `service_runtime` not running consistently at the time, since the stack bugs were not yet fixed. **That claim is superseded** |
| This project's Zigbee / OTA subsystems competing for resources | Disabling `CONFIG_ZGW_ZIGBEE_ENABLED` and `CONFIG_ZGW_OTA_ENABLED` changed nothing. (Misleading, in hindsight: those app-level flags do not disable ESP-IDF's own 802.15.4 driver or coexistence) |
| Flash Encryption itself | *Partly true, but not the cause.* It mattered only by halving the heap budget, pushing an already-latent bug over the edge |
| Coexistence (Wi-Fi ↔ 802.15.4 radio arbitration) | **The most productive wrong answer.** Disabling it moved the failure from auth to the 4-way handshake instead of fixing it — proving the problem was not RF arbitration, and pointing squarely at a shared resource. Coex was consuming memory, not airtime |

---

## 5. The method that worked

1. **Differential boot-log diff against a known-good board.** A second,
   identically-specified board connected 100 % of the time. A line-by-line
   comparison of the two boot logs isolated exactly one consistent difference:
   `coexist: coex firmware version` appeared on 16/16 failing boots and 0
   working boots.

2. **Toggle the suspect and observe *where* the failure moves.** Disabling
   `CONFIG_ESP_COEX_SW_COEXIST_ENABLE` made auth and association succeed
   immediately (`init -> auth -> assoc -> run`, RSSI −59) but moved the failure
   to a 4-way-handshake timeout (`reason=15`). A failure that *relocates*
   instead of disappearing means the toggle changed a budget, not a mechanism.

3. **Measure instead of guessing.** A temporary probe added to the
   `WIFI_EVENT_STA_DISCONNECTED` handler in
   [`hal_wifi.c`](../../components/app_hal/hal_wifi.c) reported the heap at the
   exact instant of failure:

   ```
   reason=15 [heap free8=13804 largest8=13312 freeDMA=476 largestDMA=256]
   reason=15 [heap free8=13608 largest8=13312 freeDMA=280 largestDMA=92]
   ```

   Note the trap: `MALLOC_CAP_8BIT` looked healthy at ~13 KB while DMA-capable
   memory was effectively zero. The 8BIT figure includes LP-RAM/RTCRAM, which
   DMA cannot use. **Always compare both.**

4. **Attribute the memory.** `idf.py size-components` for the per-archive
   `.bss` table, then a short script over `zigbee_gateway.map` to rank
   individual symbols — which named the exact C++ globals.

5. **Prove causality before writing the real fix.** Temporarily shrinking the
   queue to 16 entries took the same board from **0/19 to 11/11 connections
   with zero disconnects**. Only then was a permanent design change written.

---

## 6. The fixes

### 6.1 `MqttBridge` publication queue → lifecycle-scoped

The queue became an allocation claimed in `MqttBridge::start()` and released in
`stop()`, via new `ensure_publication_queue()` / `release_publication_queue()`
helpers.

The lifecycle fits the problem exactly: **MQTT starts only once the network is
already up**, which is precisely when the Wi-Fi association buffers are no
longer needed. During association the 85 KB is simply not reserved.

Deliberate properties:

- **Capacity (192) and drop-when-full semantics are unchanged.** The existing
  guards now compare against a `pending_publication_capacity_` member that is
  `0` when unallocated, so a failed allocation degrades to skipping
  publications — the same graceful degradation the bridge already applies to
  any other publication-build failure.
- **Allocation is also attempted lazily from the two staging paths.** This is
  what keeps every existing host test working unmodified, since many call
  `sync_snapshot()` without ever calling `start()`.
- **Release happens last in `stop()`**, after the worker task has exited — it
  is the only other writer, so releasing earlier would race.
- `components/mqtt_bridge` has no heap-allocation prohibition, unlike
  `components/matter_bridge` (INV-M040) or `core`/`service` (INV-H002).
  Verified by `check_arch_invariants.sh`.

### 6.2 `PersistedStateStore::probe_slot()` → `static` scratch

Fixing association exposed a stack overflow on a path that had never been
reachable before, because the device had never got as far as "network up".

`save()` calls `probe_slot()` twice while its own `WireRecord`-sized scratch is
already live in its frame — roughly **4.5 KB of stack at once**. `probe_slot()`'s
~2.2 KB local became a function-local `static`, matching the fix already applied
in `load()` and in `state_persistence_coordinator.cpp`. Every caller runs on the
single `service_runtime` task and the record is fully consumed before returning,
so no value has to survive across calls.

### 6.3 `kRuntimeTaskStackSize` → 16384

This constant had been tuned three times against real hardware:

- **16384** — fit a development build, but not production + Flash Encryption,
  where only ~12.3 KB of DMA-capable heap remained. `xTaskCreateStatic()` failed
  closed and `service_runtime` never started at all — worse than the overflow.
- **11264** — fit both profiles, but sat ~1 KB under a ceiling and was itself
  overflowed by ~1240 bytes on the newly reachable network-up path.
- **16384 again** — with ~85 KB of `.bss` freed the profile now boots with
  148 KiB instead of 66 KiB, so genuine margin over the deepest measured path
  (~12.5 KB) is affordable rather than marginal.

The lesson is in the sequence: **two of these three values were only wrong
because of the memory bug underneath.** Tuning a constant against a broken
budget produces a constant that is wrong twice.

---

## 7. Verification

Final build carried the configuration the product actually needs —
production profile, Flash Encryption, and **coexistence re-enabled** so Wi-Fi
and Zigbee can run together — flashed to the same board that had failed 0/19:

- **1 boot, no reboots at all across a 55-second window** (previously 10–16
  crash-reboots per window)
- coex firmware loaded, Wi-Fi connected, DHCP address obtained
- **0 disconnects, 0 crashes**
- mDNS advertising `https://zigbee-gateway-9f0020.local`
- production HTTPS listener serving on port 443 from persisted encrypted TLS
  material

Static checks: 109/109 host tests (unchanged count — the lazy-allocation path
kept every existing `MqttBridge` test working without modification), `cppcheck`
clean on all changed files, `check_arch_invariants.sh` `high=0 medium=0 low=0`,
real `idf.py build` clean on both profiles.

Evidence:
[`S6-cert-rotation-hil-round-stack-and-heap-fixes.json`](../../implementation-evidence/S6-cert-rotation-hil-round-stack-and-heap-fixes.json).
Security-facing narrative: [`CONTROL_PLANE_SECURITY.md`](../security/CONTROL_PLANE_SECURITY.md)
Sections 2.16–2.17.

---

## 8. Lessons worth keeping

1. **`m f <frame>` in ESP-IDF Wi-Fi logs means malloc failed.** Not auth, not RF.
2. **A failure that relocates when you toggle something is a budget problem,
   not a mechanism problem.**
3. **`MALLOC_CAP_8BIT` free size can hide DMA exhaustion**, because it counts
   LP-RAM the driver cannot use. Compare both figures.
4. **Perfect determinism is a signal.** Zero variance across many trials points
   at a resource threshold, not an analogue phenomenon like RF.
5. **Large scratch buffers do not belong in always-resident objects.** Prefer
   lifecycle-scoped ownership when the subsystem's lifetime is naturally
   narrower than the device's.
6. **Measure at the failure instant.** A five-line temporary probe in the event
   handler settled in one flash what days of hypothesis-testing had not.
7. **Tuning constants against a broken budget yields wrong constants.** Find
   the underlying resource bug first.
8. **Verify hypotheses on a known-good reference.** A second, working board
   turned an open-ended search into a two-log diff.
