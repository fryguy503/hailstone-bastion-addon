#!/usr/bin/env python3
"""Measure one reviewed executable/addon pair and prepare SQL; never connect to a database."""
import argparse
import hashlib
import json
from pathlib import Path

p = argparse.ArgumentParser(__doc__)
p.add_argument('--client-exe', type=Path, required=True)
p.add_argument('--addon', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()

def measure(path):
    before = path.stat()
    digest, fnv, length = hashlib.sha256(), 14695981039346656037, 0
    with path.open('rb') as stream:
        while chunk := stream.read(65536):
            digest.update(chunk)
            length += len(chunk)
            for byte in chunk:
                fnv = ((fnv ^ byte) * 1099511628211) & 0xffffffffffffffff
    after = path.stat()
    if not length or (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns) or length != after.st_size:
        raise SystemExit(f'Cannot measure an empty or changing binary: {path}')
    return {'file': path.name, 'bytes': length, 'sha256': digest.hexdigest(), 'fnv1a63': str(fnv & 0x7fffffffffffffff)}

exe, addon = measure(a.client_exe), measure(a.addon)
pair = exe['sha256'] + ':' + addon['sha256']
output = a.out.resolve()
output.mkdir(parents=True, exist_ok=True)
(output / 'native-integrity-binaries.json').write_text(json.dumps({'executable': exe, 'addon': addon, 'pair': pair}, indent=2) + '\n', encoding='utf-8')
sql = f'''-- Review before applying. This file approves exactly the measured preview pair.
-- It replaces this native pair setting; preserve any other approved pairs deliberately.
-- Existing legacy executable/DLL/data-file variables are not changed.
START TRANSACTION;
SELECT `id`, `varname`, `value` FROM `variables`
 WHERE `varname` = 'hailstone_bastion_pairs' FOR UPDATE;
INSERT INTO `variables` (`varname`, `value`, `information`, `ts`)
SELECT 'hailstone_bastion_pairs', '', 'Approved Hailstone executable:Bastion addon SHA256 pairs', NOW()
 WHERE NOT EXISTS (SELECT 1 FROM `variables` WHERE `varname` = 'hailstone_bastion_pairs');
UPDATE `variables` SET `value` = '{pair}',
 `information` = 'Approved Hailstone executable:Bastion addon SHA256 pairs', `ts` = NOW()
 WHERE `varname` = 'hailstone_bastion_pairs';
SELECT `varname`, `value` FROM `variables` WHERE `varname` = 'hailstone_bastion_pairs';
COMMIT;
'''
(output / 'approve-native-integrity-pair.sql').write_text(sql, encoding='utf-8')
print(f'Prepared measurements and review-only SQL in {output}; nothing was deployed.')
