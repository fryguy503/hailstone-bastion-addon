#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Measure an addon and prepare append-only legacy crc_dll SQL; never connect to a database."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import stat


MASK64 = (1 << 64) - 1
MASK63 = (1 << 63) - 1
MEASUREMENT_NAME = "legacy-integrity-addon.json"
SQL_NAME = "approve-legacy-integrity-addon.sql"


def _identity(status):
    return (status.st_dev, status.st_ino, status.st_size, status.st_mtime_ns)


def measure(path):
    """Hash every byte and reject empty, replaced or concurrently modified input."""
    path = Path(path)
    named_before = path.stat()
    digest, fnv, length = hashlib.sha256(), 14695981039346656037, 0
    with path.open("rb") as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode):
            raise ValueError("The addon must be a regular binary file")
        while chunk := stream.read(65536):
            digest.update(chunk)
            length += len(chunk)
            for byte in chunk:
                fnv = ((fnv ^ byte) * 1099511628211) & MASK64
        after = os.fstat(stream.fileno())
    named_after = path.stat()
    # On Windows Python's path stat and descriptor fstat can report different
    # creation times for the same file. Compare ctime only within each method.
    if (not length or length != after.st_size or
            len({_identity(status) for status in (named_before, before, after, named_after)}) != 1 or
            before.st_ctime_ns != after.st_ctime_ns or
            named_before.st_ctime_ns != named_after.st_ctime_ns):
        raise ValueError("Cannot measure an empty or changing addon binary")
    checksum = fnv & MASK63
    if not checksum:
        raise ValueError("A zero checksum cannot be approved: the legacy parser ignores zero tokens")
    return {"file": path.name, "bytes": length, "sha256": digest.hexdigest(), "fnv1a63": str(checksum)}


def approval_sql(checksum):
    """Use MySQL 5.7-compatible primitives and preserve the existing allowlist text."""
    if isinstance(checksum, bool) or not isinstance(checksum, int) or not 0 < checksum <= MASK63:
        raise ValueError("Expected a nonzero unsigned 63-bit checksum")
    normalized = "COALESCE(`value`, '')"
    # common/citadel_checksums.h replaces exactly these six separators before splitting on commas.
    for separator in ("'|'", "';'", "CHAR(10)", "CHAR(13)", "CHAR(9)", "' '"):
        normalized = f"REPLACE({normalized}, {separator}, ',')"
    # Strings::Trim also accepts VT/FF at token edges. Do not turn internal VT/FF into separators.
    # std::stoull accepts the corresponding negative spelling as the same uint64 value; retain it too.
    token = f"(0*{checksum}|-0*{(1 << 64) - checksum})"
    pattern = f"CONCAT('(^|,)[', CHAR(11), CHAR(12), ']*{token}[', CHAR(11), CHAR(12), ']*(,|$)')"
    return f"""-- Review before applying. This appends one measured addon to the legacy CIT1 v2 DLL allowlist.
-- Existing crc_dll values are preserved; executable and data-file CRC variables are not changed.
-- No connection or database operation was performed by the generator.
START TRANSACTION;
SELECT `id`, `varname`, `value` FROM `variables`
 WHERE `varname` = 'crc_dll' FOR UPDATE;
INSERT INTO `variables` (`varname`, `value`, `information`, `ts`)
SELECT 'crc_dll', '{checksum}', 'Approved DLL checksums for legacy CIT1 v2', NOW()
 WHERE NOT EXISTS (SELECT 1 FROM `variables` WHERE `varname` = 'crc_dll');
UPDATE `variables`
 SET `value` = CONCAT_WS(',', NULLIF(`value`, ''), '{checksum}'), `ts` = NOW()
 WHERE `varname` = 'crc_dll'
   AND NOT ({normalized} REGEXP {pattern});
SELECT `id`, `varname`, `value` FROM `variables` WHERE `varname` = 'crc_dll';
COMMIT;
"""


def prepare(addon, output):
    measurement = measure(addon)
    sql = approval_sql(int(measurement["fnv1a63"]))
    output = Path(output).resolve()
    paths = (output / MEASUREMENT_NAME, output / SQL_NAME)
    if any(path.exists() for path in paths):
        raise FileExistsError("Approval output already exists; choose a new review directory")
    output.mkdir(parents=True, exist_ok=True)
    report = {
        "schema_version": 1,
        "protocol": {"opcode": "0x7d1a", "magic": "0x43495431", "version": 2, "field": "dll"},
        "checksum_algorithm": "FNV-1a64 of all file bytes, masked with 0x7fffffffffffffff",
        "addon": measurement,
        "variable": "crc_dll",
        "approval_mode": "append without replacing existing values",
        "database_applied": False,
    }
    for path, text in zip(paths, (json.dumps(report, indent=2) + "\n", sql)):
        with path.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(text)
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--addon", type=Path, required=True, help="Actual addon binary to approve")
    parser.add_argument("--out", type=Path, required=True, help="Review directory; existing output files are never overwritten")
    args = parser.parse_args(argv)
    try:
        prepare(args.addon, args.out)
    except (OSError, ValueError) as error:
        parser.exit(2, f"{parser.prog}: {error}\n")
    print(f"Prepared measurements and review-only SQL in {args.out.resolve()}; no database was contacted.")


if __name__ == "__main__":
    main()
