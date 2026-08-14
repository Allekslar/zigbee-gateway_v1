#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Alex.K.
"""Host-only tests for verify_production_security_profile.py's parsing and
verification logic (plan S5 required change #3's "positive/negative
fixtures" test requirement). Uses only the standard library, matching this
repository's zero-extra-dependency convention for host tests. Run with:

    python3 -m unittest scripts/test_verify_production_security_profile.py

These fixtures are synthetic text, matching the shape of a real
`idf.py`-generated sdkconfig -- they prove the script's own parsing/
comparison logic is correct. The approved symbol list itself has
separately been verified against a real `idf.py`-generated sdkconfig (see
docs/security/PRODUCTION_HARDENING.md Section 2), not just against these
synthetic fixtures.

The ESP-IDF-version-check tests validate against
scripts/testdata/project_description_esp32c6_real.json, a REAL (trimmed)
`idf.py reconfigure`-generated `project_description.json` captured from
this repository's own build inside `espressif/idf:release-v5.5` -- its
`git_revision` field ("v5.5.5-316-g1a1a5aa6513") is ESP-IDF's own `git
describe` output, not a hand-written guess.
"""

import json
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import verify_production_security_profile as verifier  # noqa: E402

_TESTDATA_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "testdata")
_REAL_PROJECT_DESCRIPTION_PATH = os.path.join(_TESTDATA_DIR, "project_description_esp32c6_real.json")

APPROVED_FIXTURE = """\
#
# Automatically generated file; DO NOT EDIT.
#
CONFIG_SECURE_BOOT=y
CONFIG_SECURE_BOOT_V2_ENABLED=y
# CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES is not set
CONFIG_SECURE_FLASH_ENC_ENABLED=y
CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE=y
CONFIG_NVS_ENCRYPTION=y
CONFIG_NVS_SEC_KEY_PROTECT_USING_FLASH_ENC=y
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK=y
CONFIG_SECURE_ENABLE_SECURE_ROM_DL_MODE=y
# CONFIG_SECURE_BOOT_INSECURE is not set
# CONFIG_SECURE_BOOT_ALLOW_JTAG is not set
# CONFIG_SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT is not set
# CONFIG_ESP_COREDUMP_CAPTURE_DRAM is not set
CONFIG_ZGW_PRODUCTION_PROFILE=y
CONFIG_ZGW_COMMISSIONING_WINDOW_SECONDS=600
CONFIG_ZGW_JSON_MAX_BODY_BYTES=2048
CONFIG_ZGW_JSON_MAX_DEPTH=4
CONFIG_ZGW_JSON_MAX_STRING_BYTES=512
CONFIG_ZGW_JSON_MAX_KEYS=32
CONFIG_ZGW_LOGIN_ATTEMPTS_PER_MINUTE=5
CONFIG_ZGW_COMMANDS_PER_MINUTE=60
CONFIG_ZGW_MUTATIONS_PER_MINUTE=5
CONFIG_ZGW_FIRMWARE_OPS_PER_HOUR=2
CONFIG_ZGW_AUDIT_RING_RECORDS=128
CONFIG_ZGW_MQTT_CLIENT_ID="zigbee-gateway"
"""


class ParseSdkconfigTest(unittest.TestCase):
    def test_parses_assignments_and_not_set_lines(self):
        config = verifier.parse_sdkconfig(APPROVED_FIXTURE)
        self.assertEqual(config["CONFIG_SECURE_BOOT"], "y")
        self.assertIsNone(config["CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES"])
        self.assertIsNone(config["CONFIG_SECURE_BOOT_ALLOW_JTAG"])

    def test_strips_quotes_from_string_values(self):
        config = verifier.parse_sdkconfig(APPROVED_FIXTURE)
        self.assertEqual(config["CONFIG_ZGW_MQTT_CLIENT_ID"], "zigbee-gateway")

    def test_ignores_blank_lines_and_plain_comments(self):
        config = verifier.parse_sdkconfig("\n# just a comment, no symbol\n\nCONFIG_SECURE_BOOT=y\n")
        self.assertEqual(config, {"CONFIG_SECURE_BOOT": "y"})

    def test_ignores_non_config_lines(self):
        config = verifier.parse_sdkconfig("SOME_RANDOM_TEXT=1\nCONFIG_SECURE_BOOT=y\n")
        self.assertEqual(config, {"CONFIG_SECURE_BOOT": "y"})


class VerifyProfileTest(unittest.TestCase):
    def test_approved_fixture_passes(self):
        config = verifier.parse_sdkconfig(APPROVED_FIXTURE)
        self.assertEqual(verifier.verify_profile(config), [])

    def test_production_profile_marker_required(self):
        text = APPROVED_FIXTURE.replace(
            "CONFIG_ZGW_PRODUCTION_PROFILE=y", "# CONFIG_ZGW_PRODUCTION_PROFILE is not set")
        config = verifier.parse_sdkconfig(text)
        violations = verifier.verify_profile(config)
        self.assertTrue(any("CONFIG_ZGW_PRODUCTION_PROFILE" in v for v in violations))

    def test_missing_required_symbol_fails(self):
        text = APPROVED_FIXTURE.replace("CONFIG_SECURE_BOOT=y\n", "")
        config = verifier.parse_sdkconfig(text)
        violations = verifier.verify_profile(config)
        self.assertTrue(any("CONFIG_SECURE_BOOT must be" in v for v in violations))

    def test_wrong_value_fails(self):
        text = APPROVED_FIXTURE.replace(
            "CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE=y",
            "# CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE is not set")
        config = verifier.parse_sdkconfig(text)
        violations = verifier.verify_profile(config)
        self.assertTrue(any("CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE must be 'y'" in v for v in violations))

    def test_forbidden_symbol_present_fails(self):
        text = APPROVED_FIXTURE.replace(
            "# CONFIG_SECURE_BOOT_ALLOW_JTAG is not set", "CONFIG_SECURE_BOOT_ALLOW_JTAG=y")
        config = verifier.parse_sdkconfig(text)
        violations = verifier.verify_profile(config)
        self.assertTrue(any("CONFIG_SECURE_BOOT_ALLOW_JTAG must not be set" in v for v in violations))

    def test_forbidden_symbol_absent_entirely_is_fine(self):
        # A symbol never mentioned at all (not even as "# ... is not set")
        # must not be treated as forbidden-present -- config.get() default
        # is None, same as an explicit "not set" line.
        text = APPROVED_FIXTURE.replace("# CONFIG_SECURE_BOOT_ALLOW_JTAG is not set\n", "")
        config = verifier.parse_sdkconfig(text)
        violations = verifier.verify_profile(config)
        self.assertFalse(any("CONFIG_SECURE_BOOT_ALLOW_JTAG" in v for v in violations))

    def test_secure_boot_insecure_forbidden(self):
        text = APPROVED_FIXTURE.replace(
            "# CONFIG_SECURE_BOOT_INSECURE is not set", "CONFIG_SECURE_BOOT_INSECURE=y")
        config = verifier.parse_sdkconfig(text)
        violations = verifier.verify_profile(config)
        self.assertTrue(any("CONFIG_SECURE_BOOT_INSECURE must not be set" in v for v in violations))

    def test_flash_encryption_development_mode_forbidden(self):
        text = APPROVED_FIXTURE.replace(
            "# CONFIG_SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT is not set",
            "CONFIG_SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT=y")
        config = verifier.parse_sdkconfig(text)
        violations = verifier.verify_profile(config)
        self.assertTrue(any("CONFIG_SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT must not be set" in v for v in violations))

    def test_coredump_dram_capture_forbidden(self):
        text = APPROVED_FIXTURE.replace(
            "# CONFIG_ESP_COREDUMP_CAPTURE_DRAM is not set", "CONFIG_ESP_COREDUMP_CAPTURE_DRAM=y")
        config = verifier.parse_sdkconfig(text)
        violations = verifier.verify_profile(config)
        self.assertTrue(any("CONFIG_ESP_COREDUMP_CAPTURE_DRAM must not be set" in v for v in violations))

    def test_multiple_violations_all_reported(self):
        text = APPROVED_FIXTURE.replace("CONFIG_SECURE_BOOT=y\n", "").replace(
            "CONFIG_NVS_ENCRYPTION=y\n", "")
        config = verifier.parse_sdkconfig(text)
        violations = verifier.verify_profile(config)
        self.assertEqual(len(violations), 2)


class VerifyTunableRangesTest(unittest.TestCase):
    """Plan S6: 'Kconfig rejects out-of-range values. The production
    verifier ... verifies tunable range membership.' Exercises every one
    of the ten FD-13 tunables at its minimum/default/maximum (must pass)
    and one-below-minimum/one-above-maximum (must fail), plus the
    explicit 'in-range but non-default' carve-out and the missing-symbol
    case -- matching the plan's own boundary-test list.
    """

    def test_approved_fixture_tunables_all_pass(self):
        config = verifier.parse_sdkconfig(APPROVED_FIXTURE)
        self.assertEqual(verifier.verify_tunable_ranges(config), [])

    def test_every_tunable_at_minimum_and_maximum_passes(self):
        for symbol, minimum, maximum, _default, _ref in verifier.TUNABLE_RANGES:
            for boundary in (minimum, maximum):
                config = verifier.parse_sdkconfig(APPROVED_FIXTURE)
                config[symbol] = str(boundary)
                violations = verifier.verify_tunable_ranges(config)
                self.assertEqual(violations, [], f"{symbol}={boundary} should be in range but was rejected: {violations}")

    def test_every_tunable_one_below_minimum_and_one_above_maximum_fails(self):
        for symbol, minimum, maximum, _default, _ref in verifier.TUNABLE_RANGES:
            for out_of_range in (minimum - 1, maximum + 1):
                config = verifier.parse_sdkconfig(APPROVED_FIXTURE)
                config[symbol] = str(out_of_range)
                violations = verifier.verify_tunable_ranges(config)
                self.assertTrue(
                    any(symbol in v for v in violations),
                    f"{symbol}={out_of_range} should be rejected but was not: {violations}")

    def test_in_range_non_default_value_passes(self):
        # The plan's own explicit carve-out: "An in-range change from the
        # approved default requires a reviewed release-manifest delta...
        # rather than an automatic rejection solely for differing from
        # the default."
        config = verifier.parse_sdkconfig(APPROVED_FIXTURE)
        config["CONFIG_ZGW_COMMANDS_PER_MINUTE"] = "45"  # in [10, 60], not the default 60
        self.assertEqual(verifier.verify_tunable_ranges(config), [])

    def test_missing_tunable_fails(self):
        text = APPROVED_FIXTURE.replace("CONFIG_ZGW_AUDIT_RING_RECORDS=128\n", "")
        config = verifier.parse_sdkconfig(text)
        violations = verifier.verify_tunable_ranges(config)
        self.assertTrue(any("CONFIG_ZGW_AUDIT_RING_RECORDS" in v for v in violations))

    def test_unparseable_tunable_fails(self):
        text = APPROVED_FIXTURE.replace("CONFIG_ZGW_AUDIT_RING_RECORDS=128", 'CONFIG_ZGW_AUDIT_RING_RECORDS="oops"')
        config = verifier.parse_sdkconfig(text)
        violations = verifier.verify_tunable_ranges(config)
        self.assertTrue(any("not parseable as an integer" in v for v in violations))


class RepositoryLayeredProfileTest(unittest.TestCase):
    """Simulates idf.py's layered SDKCONFIG_DEFAULTS merge (a plain
    concatenation of sdkconfig.defaults + sdkconfig.defaults.esp32c6 +
    sdkconfig.production.esp32c6, later files' assignments winning on
    conflict since parse_sdkconfig() processes lines in order and a later
    key overwrites an earlier one in the resulting dict) and checks the
    result passes the verifier. This is NOT a substitute for a real
    idf.py-generated sdkconfig (real Kconfig dependency resolution can
    differ from a naive concatenation), but it does catch drift if any of
    the three files stops producing the approved profile together.
    """

    def test_layered_defaults_produce_an_approved_profile(self):
        repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        parts = []
        for name in ("sdkconfig.defaults", "sdkconfig.defaults.esp32c6", "sdkconfig.production.esp32c6"):
            with open(os.path.join(repo_root, name), "r", encoding="utf-8") as handle:
                parts.append(handle.read())
        merged = "\n".join(parts)
        config = verifier.parse_sdkconfig(merged)
        violations = verifier.verify_profile(config)
        self.assertEqual(violations, [], f"layered production defaults do not pass the verifier: {violations}")


class ParseIdfVersionTest(unittest.TestCase):
    def test_parses_real_git_describe_output(self):
        self.assertEqual(verifier.parse_idf_version("v5.5.5-316-g1a1a5aa6513"), (5, 5, 5))

    def test_parses_plain_tag(self):
        self.assertEqual(verifier.parse_idf_version("v5.5.2"), (5, 5, 2))

    def test_parses_dirty_suffixed_tag(self):
        self.assertEqual(verifier.parse_idf_version("v5.5.5-dirty"), (5, 5, 5))

    def test_unparseable_string_returns_none(self):
        self.assertIsNone(verifier.parse_idf_version("not-a-version-string"))


class VerifyIdfVersionTest(unittest.TestCase):
    def test_matching_minor_with_different_patch_passes(self):
        # This project's CI tracks espressif/idf:release-v5.5 (a branch-
        # tracking tag), which currently resolves to 5.5.5 -- not the
        # plan's originally written "exactly 5.5.2". Any 5.5.x must pass.
        self.assertEqual(verifier.verify_idf_version((5, 5, 5), "v5.5.5-316-g1a1a5aa6513"), [])
        self.assertEqual(verifier.verify_idf_version((5, 5, 2), "v5.5.2"), [])
        self.assertEqual(verifier.verify_idf_version((5, 5, 0), "v5.5.0"), [])

    def test_wrong_minor_fails(self):
        violations = verifier.verify_idf_version((5, 4, 1), "v5.4.1")
        self.assertTrue(any("5.5.x" in v for v in violations))

    def test_wrong_major_fails(self):
        violations = verifier.verify_idf_version((6, 0, 0), "v6.0.0")
        self.assertTrue(any("5.5.x" in v for v in violations))

    def test_unparseable_version_fails(self):
        violations = verifier.verify_idf_version(None, "garbage")
        self.assertTrue(any("could not parse" in v for v in violations))


class ReadIdfVersionFromProjectDescriptionTest(unittest.TestCase):
    def test_reads_the_real_captured_fixture(self):
        version, raw = verifier.read_idf_version_from_project_description(_REAL_PROJECT_DESCRIPTION_PATH)
        self.assertEqual(version, (5, 5, 5))
        self.assertEqual(raw, "v5.5.5-316-g1a1a5aa6513")

    def test_real_fixture_passes_verify_idf_version(self):
        version, raw = verifier.read_idf_version_from_project_description(_REAL_PROJECT_DESCRIPTION_PATH)
        self.assertEqual(verifier.verify_idf_version(version, raw), [])

    def test_missing_git_revision_key_raises(self):
        import tempfile

        with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as handle:
            json.dump({"target": "esp32c6"}, handle)
            path = handle.name
        try:
            with self.assertRaises(KeyError):
                verifier.read_idf_version_from_project_description(path)
        finally:
            os.unlink(path)


class MainTest(unittest.TestCase):
    def _write_fixture(self, tmp_path: str, text: str) -> None:
        with open(tmp_path, "w", encoding="utf-8") as handle:
            handle.write(text)

    def test_main_returns_zero_for_approved_fixture(self):
        import tempfile

        with tempfile.NamedTemporaryFile(mode="w", suffix=".sdkconfig", delete=False) as handle:
            handle.write(APPROVED_FIXTURE)
            path = handle.name
        try:
            self.assertEqual(verifier.main([path]), 0)
        finally:
            os.unlink(path)

    def test_main_returns_one_for_violating_fixture(self):
        import tempfile

        with tempfile.NamedTemporaryFile(mode="w", suffix=".sdkconfig", delete=False) as handle:
            handle.write(APPROVED_FIXTURE.replace("CONFIG_SECURE_BOOT=y\n", ""))
            path = handle.name
        try:
            self.assertEqual(verifier.main([path]), 1)
        finally:
            os.unlink(path)

    def test_main_returns_two_for_missing_file(self):
        self.assertEqual(verifier.main(["/nonexistent/path/sdkconfig"]), 2)

    def test_main_without_project_description_skips_version_check(self):
        # No --project-description given: an approved sdkconfig still
        # passes even though its ESP-IDF version was never checked.
        import tempfile

        with tempfile.NamedTemporaryFile(mode="w", suffix=".sdkconfig", delete=False) as handle:
            handle.write(APPROVED_FIXTURE)
            path = handle.name
        try:
            self.assertEqual(verifier.main([path]), 0)
        finally:
            os.unlink(path)

    def test_main_with_real_project_description_passes(self):
        import tempfile

        with tempfile.NamedTemporaryFile(mode="w", suffix=".sdkconfig", delete=False) as handle:
            handle.write(APPROVED_FIXTURE)
            path = handle.name
        try:
            self.assertEqual(
                verifier.main([path, "--project-description", _REAL_PROJECT_DESCRIPTION_PATH]), 0)
        finally:
            os.unlink(path)

    def test_main_with_wrong_idf_version_fails(self):
        import tempfile

        with tempfile.NamedTemporaryFile(mode="w", suffix=".sdkconfig", delete=False) as handle:
            handle.write(APPROVED_FIXTURE)
            sdkconfig_path = handle.name
        with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as handle:
            json.dump({"git_revision": "v5.4.1-1-gabc1234"}, handle)
            project_description_path = handle.name
        try:
            self.assertEqual(
                verifier.main([sdkconfig_path, "--project-description", project_description_path]), 1)
        finally:
            os.unlink(sdkconfig_path)
            os.unlink(project_description_path)

    def test_main_returns_two_for_missing_project_description(self):
        import tempfile

        with tempfile.NamedTemporaryFile(mode="w", suffix=".sdkconfig", delete=False) as handle:
            handle.write(APPROVED_FIXTURE)
            path = handle.name
        try:
            self.assertEqual(
                verifier.main([path, "--project-description", "/nonexistent/project_description.json"]), 2)
        finally:
            os.unlink(path)


if __name__ == "__main__":
    unittest.main()
