# Native integration checks

The current Bastion server uses **CIT1 version 2** to check a DLL checksum. The addon reports the
actual loaded `bastion.dll` with that protocol. `/bastion` Settings shows a submitted legacy report
as **Server unconfirmed**, because version 2 has no acknowledgement. Submitted means the native
client host accepted the send request; it does not prove delivery or server acceptance.

The addon also supports a negotiated **CIT1 version 3** WORLD handshake, with a separate optional
server change. The server sends a fresh connection nonce, the addon measures the actual Hailstone
executable and its own loaded binary, and the server checks their SHA256 pair against its approved
builds. Only the explicit version 3 acknowledgement can display **Accepted** in Settings.
The version 3 wire format requires SDK ABI 3. Preview 12 uses ABI 4 and reports **Unsupported**
if a server requests version 3. Bastion's version 2 DLL report uses the measured binary as usual.

## Current server: legacy DLL report

On a selected Bastion login, the addon measures its loaded DLL and reports its FNV-1a 63-bit
checksum before the first outgoing CRC1 packet. EnterWorld is a fallback if that CRC1 trigger was
not observed. The version 2 body is exactly 16 bytes: little-endian `u32 magic=0x43495431`,
`u32 version=2`, `u64 fnv1a63`, on WORLD opcode `0x7d1a`. There is no opcode prefix in the SDK payload.

The report is submitted once per login, with at most three send attempts spaced one second apart if
the host rejects the send request. The legacy measurement is cached for that login; hidden panels
and ordinary frames do not rehash the binary. Settings keeps measured, queued and submitted states
separate, and its binary details
show the actual `bastion.dll` byte count, decimal FNV63 checksum and addon version. This legacy report
checks the addon DLL only; it does not report a running executable/addon SHA256 pair.

This check belongs to a fresh WORLD login. Bastion's server ignores integrity packets during zone
transfers (`LoginInfo.zoning=1`). The addon retains a completed login's original receipt and digest
across those transfers; it does not remeasure or resubmit the DLL there. Pending reports are cancelled
when their login connection ends. If the addon first loads after zone entry, Settings shows
**Reconnect required** and **Not submitted**: return to the login screen and reconnect after approving
the DLL. An ignored transfer packet must not stand in for the missed login report.

Preview 4 could submit during a transfer after late approval, showing **Submitted** even though the
server intentionally discarded the report and left `account.crc_dll` unchanged. This sequence was
reproduced with the real native loader and loopback transport; it is fixed in Preview 5.

The server's `variables.crc_dll` whitelist must include the newly measured `bastion.dll` checksum.
Append the new value while preserving every existing approved legacy `dinput8.dll` checksum; replacing
the list would reject those existing clients. A rebuilt DLL needs its own newly measured checksum.
Do not rename the addon to `dinput8.dll`, claim that DLL is loaded, or reuse a checksum from another binary.

Prepare the measured legacy approval files with:

```
python tools/prepare_legacy_integrity_approval.py --addon /path/to/bastion.dll --out /path/to/new-review
```

This writes `legacy-integrity-addon.json` and review-only `approve-legacy-integrity-addon.sql`.
The SQL appends the measured checksum to `variables.crc_dll` while retaining existing values and
avoiding an equivalent duplicate. It does not alter executable/data-file CRC settings. The tool
does not connect to a database, apply SQL, or overwrite existing approval output.

A valid version 3 challenge takes priority and suppresses the legacy report for that login, including
when required features are unsupported or the native report is rejected. A rejected native build
cannot fall back to legacy approval. The version 3 server patch described below remains separate
and optional; the legacy report does not install or require it.

## Native version 3 measurement

The host measures its current executable file and the loader's private copy of this addon. The
addon copy must match the bytes admitted by the plugin loader. The API takes only these two known
subjects and exposes no arbitrary filesystem paths. It reports byte counts, SHA256 and the legacy
FNV-1a 63-bit digest. Missing or changing files cannot produce a successful measurement.

The existing game-folder `eqgame.exe`, `Resources/SkillCaps.txt` and `Resources/BaseData.txt` CRCs
continue through Hailstone's ordinary native CRC1/2/3 packets. They do not substitute for checking
the running Hailstone executable. The native report identifies the integrated Bastion addon and
does not claim that the legacy `dinput8.dll` is loaded. No hardware identifier is collected.

## Server integration

Prepare the optional version 3 change for the inspected legacy version 2 server with:

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

## Version 3 wire contract

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
and game-file receipts on a fresh non-zoning authentication. The addon sends a version 3 report only
after a selected Bastion peer's valid challenge. Unknown required bits, zero or
stale nonces, malformed packets, unavailable measurements and unapproved pairs cannot pass. The
challenge lasts 60 seconds, send failures have at most three attempts, and a missing acknowledgement
times out separately. Peer/world-login resets retire the prior state. Replies echo the request nonce.
An old server sends no challenge, so the addon never sends an unsupported version 3 report to it.
