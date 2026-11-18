# SPDX-License-Identifier: MIT
"""Standard-library tool regressions; SQL semantics use SQLite compatibility shims, not a real server."""

import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sqlite3
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock


TOOL = Path(__file__).resolve().parents[1] / "tools" / "prepare_legacy_integrity_approval.py"
SPEC = importlib.util.spec_from_file_location("prepare_legacy_integrity_approval", TOOL)
APPROVAL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(APPROVAL)


class MeasurementTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="bastion-legacy-approval-test-")
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)

    def test_known_fnv_and_sha_vectors(self):
        for data, fnv64 in ((b"a", 0xaf63dc4c8601ec8c), (b"hello", 0xa430d84680aabd0b),
                            (b"foobar", 0x85944171f73967e8), (b"\0", 0xaf63bd4c8601b7df)):
            with self.subTest(data=data):
                binary = self.root / "bastion.dll"
                binary.write_bytes(data)
                measured = APPROVAL.measure(binary)
                self.assertEqual(measured["fnv1a63"], str(fnv64 & ((1 << 63) - 1)))
                self.assertEqual(measured["sha256"], hashlib.sha256(data).hexdigest())
                self.assertEqual(measured["bytes"], len(data))
                self.assertEqual(measured["file"], "bastion.dll")

    def test_all_bytes_across_chunk_boundaries(self):
        binary = self.root / "bastion.dll"
        data = bytes(range(256)) * 513
        binary.write_bytes(data)
        measured = APPROVAL.measure(binary)
        self.assertEqual(measured["bytes"], len(data))
        self.assertEqual(measured["sha256"], hashlib.sha256(data).hexdigest())

    def test_stat_and_fstat_creation_times_may_differ_but_each_must_be_stable(self):
        binary = self.root / "bastion.dll"
        binary.write_bytes(b"hello")
        named = binary.stat()
        descriptor = SimpleNamespace(**{name: getattr(named, name) for name in
                                       ("st_dev", "st_ino", "st_size", "st_mtime_ns", "st_ctime_ns", "st_mode")})
        descriptor.st_ctime_ns += 1
        with mock.patch.object(APPROVAL.os, "fstat", side_effect=(descriptor, descriptor)):
            self.assertEqual(APPROVAL.measure(binary)["sha256"], hashlib.sha256(b"hello").hexdigest())
        changed = SimpleNamespace(**vars(descriptor))
        changed.st_ctime_ns += 1
        with mock.patch.object(APPROVAL.os, "fstat", side_effect=(descriptor, changed)):
            with self.assertRaisesRegex(ValueError, "empty or changing"):
                APPROVAL.measure(binary)

    def test_empty_missing_and_changed_files_are_refused(self):
        binary = self.root / "bastion.dll"
        with self.assertRaises(OSError):
            APPROVAL.measure(binary)
        binary.touch()
        with self.assertRaisesRegex(ValueError, "empty or changing"):
            APPROVAL.measure(binary)
        binary.write_bytes(b"hello")
        before = binary.stat()
        changed = SimpleNamespace(**{name: getattr(before, name) for name in
                                    ("st_dev", "st_ino", "st_size", "st_mtime_ns", "st_ctime_ns", "st_mode")})
        changed.st_mtime_ns += 1
        with mock.patch.object(APPROVAL.os, "fstat", side_effect=(before, changed)):
            with self.assertRaisesRegex(ValueError, "empty or changing"):
                APPROVAL.measure(binary)

    def test_cli_writes_review_only_artifacts_without_absolute_paths(self):
        binary = self.root / "bastion's addon.dll"
        binary.write_bytes(b"hello")
        output = self.root / "review"
        command = [sys.executable, str(TOOL), "--addon", str(binary), "--out", str(output)]
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("no database was contacted", result.stdout)
        report = json.loads((output / APPROVAL.MEASUREMENT_NAME).read_text(encoding="utf-8"))
        sql = (output / APPROVAL.SQL_NAME).read_text(encoding="utf-8")
        self.assertFalse(report["database_applied"])
        self.assertEqual(report["protocol"]["version"], 2)
        self.assertEqual(report["addon"]["file"], binary.name)
        self.assertNotIn(str(self.root), json.dumps(report))
        self.assertNotIn(binary.name, sql)
        self.assertIn(report["addon"]["fnv1a63"], sql)
        original = {path.name: path.read_bytes() for path in output.iterdir()}
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual({path.name: path.read_bytes() for path in output.iterdir()}, original)


class SqlTests(unittest.TestCase):
    def setUp(self):
        self.db = sqlite3.connect(":memory:")
        self.addCleanup(self.db.close)
        self.db.create_function("CONCAT_WS", -1, lambda sep, *values: sep.join(str(v) for v in values if v is not None))
        self.db.create_function("CONCAT", -1, lambda *values: "".join(str(v) for v in values))
        self.db.create_function("NOW", 0, lambda: "review-time")
        self.db.create_function("REGEXP", 2, lambda pattern, value: re.search(pattern, value) is not None)
        self.db.execute("CREATE TABLE variables(id INTEGER PRIMARY KEY, varname TEXT UNIQUE, value TEXT, information TEXT, ts TEXT)")
        for name in ("crc_eqgame", "crc_skillcaps", "crc_basedata", "hailstone_bastion_pairs"):
            self.db.execute("INSERT INTO variables(varname,value,information,ts) VALUES(?,?,?,?)",
                            (name, "keep-" + name, "original-info", "original-time"))
        self.db.commit()

    def apply(self, checksum):
        # Only transaction/row-lock syntax differs; functions and token predicate execute unchanged.
        sql = APPROVAL.approval_sql(checksum).replace("START TRANSACTION;", "BEGIN;").replace(" FOR UPDATE;", ";")
        self.db.executescript(sql)

    def insert(self, value):
        self.db.execute("INSERT INTO variables(varname,value,information,ts) VALUES('crc_dll',?,'legacy-info','legacy-time')", (value,))
        self.db.commit()

    def value(self):
        return self.db.execute("SELECT value FROM variables WHERE varname='crc_dll'").fetchone()[0]

    def test_missing_variable_is_inserted_once_and_other_crcs_unchanged(self):
        before = self.db.execute("SELECT * FROM variables ORDER BY id").fetchall()
        self.apply(123)
        self.apply(123)
        self.assertEqual(self.value(), "123")
        self.assertEqual(self.db.execute("SELECT COUNT(*) FROM variables WHERE varname='crc_dll'").fetchone()[0], 1)
        self.assertEqual(self.db.execute("SELECT * FROM variables WHERE varname!='crc_dll' ORDER BY id").fetchall(), before)

    def test_existing_allowlist_text_and_metadata_are_preserved_when_appending(self):
        original = "7|8;9\n10\r11\t12 13"
        self.insert(original)
        self.apply(123)
        self.assertEqual(self.value(), original + ",123")
        self.assertEqual(self.db.execute("SELECT information FROM variables WHERE varname='crc_dll'").fetchone()[0], "legacy-info")
        self.apply(123)
        self.assertEqual(self.value(), original + ",123")

    def test_every_server_separator_and_numeric_spelling_prevents_duplicates(self):
        spellings = ("123", "000123", "-" + str((1 << 64) - 123), "\v\f000123\f\v")
        for separator in (",", "|", ";", "\n", "\r", "\t", " "):
            for spelling in spellings:
                with self.subTest(separator=repr(separator), spelling=repr(spelling)):
                    self.db.execute("DELETE FROM variables WHERE varname='crc_dll'")
                    original = separator.join(("9", spelling, "8"))
                    self.insert(original)
                    self.apply(123)
                    self.assertEqual(self.value(), original)
                    self.assertEqual(self.db.execute("SELECT ts FROM variables WHERE varname='crc_dll'").fetchone()[0], "legacy-time")

    def test_substrings_invalid_numbers_and_internal_trim_characters_do_not_hide_new_token(self):
        for original in ("9123,1234", "+123", "0x7b", "123suffix", "9\v123", "123\f9"):
            with self.subTest(value=repr(original)):
                self.db.execute("DELETE FROM variables WHERE varname='crc_dll'")
                self.insert(original)
                self.apply(123)
                self.assertEqual(self.value(), original + ",123")

    def test_empty_null_and_zero_values_accept_the_new_checksum(self):
        for original in (None, "", "0"):
            with self.subTest(value=original):
                self.db.execute("DELETE FROM variables WHERE varname='crc_dll'")
                self.insert(original)
                self.apply(123)
                self.assertEqual(self.value(), "0,123" if original == "0" else "123")

    def test_large_checksum_is_compared_as_an_exact_decimal_token(self):
        checksum = (1 << 63) - 1
        self.insert("9223372036854775806")
        self.apply(checksum)
        self.apply(checksum)
        self.assertEqual(self.value(), "9223372036854775806,9223372036854775807")

    def test_invalid_checksum_cannot_enter_sql(self):
        for value in (0, -1, 1 << 63, "123'; DROP TABLE variables;", True):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    APPROVAL.approval_sql(value)


if __name__ == "__main__":
    unittest.main()
