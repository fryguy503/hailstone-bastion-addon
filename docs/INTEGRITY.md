# Native integration checks

The addon supports a negotiated **CIT1 version 3** WORLD handshake. The server sends a fresh
connection nonce, the addon measures the actual Hailstone executable and its own loaded binary,
and the server checks their SHA256 pair against its approved builds. An explicit acknowledgement
controls the status shown in `/bastion` Settings. Sending a report alone never means it passed.

The host measures its current executable file and the loader's private copy of this addon. The
addon copy must match the bytes admitted by the plugin loader. The API takes only these two known
subjects and exposes no arbitrary filesystem paths. It reports byte counts, SHA256 and the legacy
FNV-1a 63-bit digest. Missing or changing files cannot produce a successful measurement.

The existing game-folder `eqgame.exe`, `Resources/SkillCaps.txt` and `Resources/BaseData.txt` CRCs
continue through Hailstone's ordinary native CRC1/2/3 packets. They do not substitute for checking
the running Hailstone executable. The native report identifies the integrated Bastion addon and
does not claim that the legacy `dinput8.dll` is loaded. No hardware identifier is collected.

## Server integration

The inspected server understands legacy CIT1 version 2. Prepare the matching version 3 change with:

```
python tools/prepare_server_integrity.py --server /path/to/server --out /path/to/isolated-review
```

This writes a review patch, modified copies of `world/client.cpp` and `world/client.h`, the portable
MIT-licensed `common/hailstone_native_integrity.h`, and hashes of the inspected base files. It never
edits the supplied server checkout or deploys a binary. Existing version 2 handling stays intact.

After reviewing and building that server change, approve the exact client/addon pair:

```
python tools/prepare_integrity_approval.py --client-exe /path/to/hailstone.exe --addon /path/to/bastion.dll --out /path/to/review
```

The tool produces measured JSON and review-only SQL for `variables.hailstone_bastion_pairs`.
The generated SQL approves one pair and replaces that native setting; retain other approved pairs
deliberately when maintaining multiple builds. The parser also accepts delimited `EXE_SHA256:ADDON_SHA256`
pairs. An empty setting approves no native build and sends no challenge. The legacy allowlists and
verification rules are preserved. Build changes require reviewing and approving their new pair.

On acceptance the server logs the account, addon version, feature mask, both digests and byte counts,
marks the native addon requirement satisfied, and still waits for the existing three game-file checks.
This records a startup integrity report. It is not hardware-backed remote attestation or continuous
proof that process memory is unchanged.

## Wire contract

All values are little endian on WORLD opcode `0x7d1a`; there is no opcode prefix in the SDK payload.
Every header is 24 bytes: `u32 magic=0x43495431`, `u32 version=3`, `u32 kind`, `u32 flags`, `u64 nonce`.

| Kind | Exact size | Payload after header |
|---|---:|---|
| Challenge (1) | 32 | `u64 required_features`; flags zero |
| Report (2) | 168 | `u64 features`, `u32 SDK_ABI=3`, `u32 reserved=0`, executable measurement, addon measurement, NUL-terminated/padded ASCII addon version[32]; flags zero |
| Acknowledgement (3) | 152 | NUL-terminated/padded ASCII reason[128]; flags 1 accepted, 2 rejected, 3 unsupported, 4 malformed |

A measurement is 48 bytes: `u64 byte_count`, `u64 fnv1a63`, `u8 sha256[32]`.
The current feature mask is `0x3fff`: integrated native addon, follow, merchant selling, Tracking maps,
raid bars, factions, DZ, pet gear, Kinbound, ratings, home city, telegraphs, Firefell and command browser.
These bits describe supported features; optional merchant selling remains disabled by default.

The server binds each challenge to the authenticated account and current login, and retires native
and game-file receipts on a fresh non-zoning authentication. The addon sends only after a selected
Bastion peer's valid challenge. Unknown required bits, zero or
stale nonces, malformed packets, unavailable measurements and unapproved pairs cannot pass. The
challenge lasts 60 seconds, send failures have at most three attempts, and a missing acknowledgement
times out separately. Peer/world-login resets retire the prior state. Replies echo the request nonce.
An old server sends no challenge, so the addon never sends an unsupported version 3 report to it.
