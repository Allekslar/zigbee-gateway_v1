#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Alex.K.
"""Host-only tests for efuse_provisioning_template.py (plan S5 required
change #5's template/schema). Uses only the standard library. Run with:

    python3 -m unittest scripts/test_efuse_provisioning_template.py

Validates against scripts/testdata/espefuse_esp32c6_virtual_summary.json,
a REAL `espefuse.py --chip esp32c6 --virt summary --format json` capture
(ESP-IDF 5.5.5's own eFuse emulation, no hardware involved) -- not a
hand-written fixture, so these tests prove the template's field names
actually exist in real ESP-IDF tooling output, not just that this
module's own logic is internally consistent.
"""

import copy
import json
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import efuse_provisioning_template as template  # noqa: E402

_TESTDATA_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "testdata")
_REAL_SUMMARY_PATH = os.path.join(_TESTDATA_DIR, "espefuse_esp32c6_virtual_summary.json")


def _load_real_summary() -> dict:
    with open(_REAL_SUMMARY_PATH, "r", encoding="utf-8") as handle:
        return json.load(handle)


class RealFixtureTest(unittest.TestCase):
    """Confirms every field name this module references actually exists
    in real ESP-IDF 5.5.5 espefuse.py output -- catching the exact kind of
    invented-symbol mistake found (and fixed) in
    verify_production_security_profile.py's own history.
    """

    def test_all_referenced_fields_exist_in_real_summary(self):
        summary = _load_real_summary()
        all_fields = (
            template.CHIP_IDENTITY_FIELDS
            + template.SECURE_BOOT_FIELDS
            + template.FLASH_ENCRYPTION_FIELDS
            + template.ANTI_ROLLBACK_FIELDS
            + template.JTAG_DOWNLOAD_POLICY_FIELDS
            + template.KEY_MATERIAL_BLOCK_FIELDS
        )
        missing = [name for name in all_fields if name not in summary]
        self.assertEqual(missing, [], f"fields not present in the real espefuse.py summary: {missing}")

    def test_key_material_fields_are_real_256_bit_blocks(self):
        summary = _load_real_summary()
        for name in template.KEY_MATERIAL_BLOCK_FIELDS:
            self.assertEqual(summary[name]["bit_len"], 256)
            self.assertEqual(summary[name]["category"], "security")


class RedactKeyMaterialTest(unittest.TestCase):
    def test_redacts_zeroed_key(self):
        summary = _load_real_summary()
        redacted = template.redact_key_material(summary["BLOCK_KEY0"])
        self.assertTrue(redacted["redacted"])
        self.assertEqual(len(redacted["sha256_digest"]), 64)
        self.assertNotIn("value", redacted)
        self.assertNotIn("raw_value", redacted)

    def test_different_key_bytes_produce_different_digests(self):
        field_a = {"raw_value": "0x" + "00" * 32, "readable": True, "writeable": True}
        field_b = {"raw_value": "0x" + "ff" * 32, "readable": True, "writeable": True}
        digest_a = template.redact_key_material(field_a)["sha256_digest"]
        digest_b = template.redact_key_material(field_b)["sha256_digest"]
        self.assertNotEqual(digest_a, digest_b)

    def test_same_key_bytes_produce_the_same_digest(self):
        field = {"raw_value": "0x" + "ab" * 32, "readable": True, "writeable": True}
        digest_1 = template.redact_key_material(field)["sha256_digest"]
        digest_2 = template.redact_key_material(field)["sha256_digest"]
        self.assertEqual(digest_1, digest_2)


class BuildProvisioningRecordTest(unittest.TestCase):
    def setUp(self):
        self.summary = _load_real_summary()
        self.record = template.build_provisioning_record(self.summary, "0012347788aa", "2026-08-08T00:00:00Z")

    def test_record_is_valid(self):
        self.assertEqual(template.validate_provisioning_record(self.record), [])

    def test_record_has_all_sections(self):
        for section_name, _fields in template._CATEGORY_SECTIONS:  # noqa: SLF001 -- intentional white-box test
            self.assertIn(section_name, self.record)

    def test_chip_identity_reflects_real_mac_field(self):
        self.assertIn("MAC", self.record["chip_identity"])
        self.assertEqual(self.record["chip_identity"]["MAC"]["value"], self.summary["MAC"]["value"])

    def test_key_material_is_redacted_not_raw(self):
        for name in template.KEY_MATERIAL_BLOCK_FIELDS:
            entry = self.record["key_material_digests"][name]
            self.assertTrue(entry["redacted"])
            self.assertNotIn("value", entry)
            self.assertNotIn("raw_value", entry)

    def test_missing_espefuse_field_is_simply_omitted_not_fabricated(self):
        sparse_summary = {"MAC": self.summary["MAC"]}
        record = template.build_provisioning_record(sparse_summary, "0012347788aa", "2026-08-08T00:00:00Z")
        self.assertIn("MAC", record["chip_identity"])
        self.assertNotIn("WAFER_VERSION_MAJOR", record["chip_identity"])
        # A record missing required fields must fail validation loudly,
        # never silently claim completeness.
        violations = template.validate_provisioning_record(record)
        self.assertTrue(any("WAFER_VERSION_MAJOR" in v for v in violations))


class ValidateProvisioningRecordTest(unittest.TestCase):
    def setUp(self):
        summary = _load_real_summary()
        self.valid_record = template.build_provisioning_record(summary, "0012347788aa", "2026-08-08T00:00:00Z")

    def test_wrong_schema_version_fails(self):
        record = copy.deepcopy(self.valid_record)
        record["schema_version"] = 2
        violations = template.validate_provisioning_record(record)
        self.assertTrue(any("schema_version" in v for v in violations))

    def test_malformed_gateway_id_fails(self):
        for bad_id in ["short", "0012347788AA", "0012347788aa1", "", None]:
            record = copy.deepcopy(self.valid_record)
            record["gateway_id"] = bad_id
            violations = template.validate_provisioning_record(record)
            self.assertTrue(any("gateway_id" in v for v in violations), f"expected violation for {bad_id!r}")

    def test_missing_captured_at_fails(self):
        record = copy.deepcopy(self.valid_record)
        record["captured_at"] = ""
        violations = template.validate_provisioning_record(record)
        self.assertTrue(any("captured_at" in v for v in violations))

    def test_missing_section_fails(self):
        record = copy.deepcopy(self.valid_record)
        del record["secure_boot"]
        violations = template.validate_provisioning_record(record)
        self.assertTrue(any("secure_boot" in v for v in violations))

    def test_leaked_raw_key_material_fails(self):
        record = copy.deepcopy(self.valid_record)
        record["key_material_digests"]["BLOCK_KEY0"]["value"] = "00 11 22 33"
        violations = template.validate_provisioning_record(record)
        self.assertTrue(any("leaks raw key material" in v for v in violations))

    def test_unredacted_key_material_fails(self):
        record = copy.deepcopy(self.valid_record)
        record["key_material_digests"]["BLOCK_KEY0"]["redacted"] = False
        violations = template.validate_provisioning_record(record)
        self.assertTrue(any("must be marked redacted" in v for v in violations))


class MainCliTest(unittest.TestCase):
    def test_build_then_validate_round_trip(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp_dir:
            output_path = os.path.join(tmp_dir, "record.json")
            build_exit = template.main(
                [
                    "build",
                    _REAL_SUMMARY_PATH,
                    "--gateway-id",
                    "0012347788aa",
                    "--captured-at",
                    "2026-08-08T00:00:00Z",
                    "--output",
                    output_path,
                ])
            self.assertEqual(build_exit, 0)
            self.assertTrue(os.path.isfile(output_path))

            validate_exit = template.main(["validate", output_path])
            self.assertEqual(validate_exit, 0)

    def test_validate_reports_failure_for_invalid_record(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp_dir:
            bad_record_path = os.path.join(tmp_dir, "bad_record.json")
            with open(bad_record_path, "w", encoding="utf-8") as handle:
                json.dump({"schema_version": 1}, handle)
            exit_code = template.main(["validate", bad_record_path])
            self.assertEqual(exit_code, 1)


if __name__ == "__main__":
    unittest.main()
