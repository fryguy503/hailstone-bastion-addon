#!/usr/bin/env python3
"""Prepare an isolated world-server integration patch; never edit or deploy the server."""
import argparse
import difflib
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(__doc__)
p.add_argument('--server', type=Path, required=True, help='Inspected Bastion server source')
p.add_argument('--out', type=Path, required=True, help='New isolated review directory')
a = p.parse_args()
source, output = a.server.resolve(), a.out.resolve()
if output == source or output.is_relative_to(source) or source.is_relative_to(output):
    p.error('Use an isolated output directory outside the server source')
output.mkdir(parents=True, exist_ok=True)
patch, hashes = [], {}

def replace_once(text, old, new):
    if text.count(old) != 1:
        raise SystemExit(f'Server source changed: expected one integration anchor: {old[:100]!r}')
    return text.replace(old, new, 1)

cpp = (source / 'world/client.cpp').read_text(encoding='utf-8')
cpp = replace_once(cpp, '#include "../common/citadel_checksums.h"',
                  '#include "../common/citadel_checksums.h"\n#include "../common/hailstone_native_integrity.h"')
cpp = replace_once(cpp, 'void Client::SendCharInfo() {',
                  'void Client::SendCharInfo() {\n  SendHailstoneIntegrityChallenge();')
cpp = replace_once(cpp, '  if ((cle = client_list.CheckAuth(id, password))) {',
                  '''  if ((cle = client_list.CheckAuth(id, password))) {
    if (!is_player_zoning) {
      // A new authentication requires fresh reports, even on a reused socket.
      hailstone_integrity = bastion_native_integrity::Session{};
      citadel_integrity_failed = false;
      citadel_crc1_passed = citadel_crc2_passed = citadel_crc3_passed = false;
      citadel_dll_passed = false;
      citadel_clean_char_info_sent = citadel_patch_char_info_sent = false;
    }''')
cpp = replace_once(cpp, 'bool Client::HandleCitadelIntegrityPacket(const EQApplicationPacket *app) {',
                  '''bool Client::HandleCitadelIntegrityPacket(const EQApplicationPacket *app) {
  const auto native_bytes = std::span<const std::uint8_t>(app->pBuffer, app->size);
  if (bastion_native_integrity::is_native(native_bytes)) {
    return HandleHailstoneIntegrityPacket(app);
  }''')
methods = '''
// Native integration uses an explicit pair of actual executable/addon digests.
// The legacy CIT1 v2 and native game-data CRC1/2/3 paths remain unchanged.
void Client::SendHailstoneIntegrityChallenge() {
  namespace native = bastion_native_integrity;
  const auto now = Timer::GetCurrentTime();
  if (is_player_zoning || !GetAccountID() ||
      m_ClientVersion != EQ::versions::ClientVersion::RoF2 ||
      hailstone_integrity.verified() || hailstone_integrity.fresh(now)) return;
  auto configured = database.QueryDatabase(
      "SELECT `value` FROM `variables` WHERE `varname` = 'hailstone_bastion_pairs' LIMIT 1");
  if (!configured.Success() || !configured.RowCount()) return;
  auto row = configured.begin();
  if (!row[0] || !*row[0]) return;
  // Connection-local freshness, not a hardware identifier or attestation key.
  const auto nonce = (static_cast<uint64>(emu_random.Int(1, INT_MAX)) << 32) |
      static_cast<uint32>(emu_random.Int(1, INT_MAX));
  hailstone_integrity.issue(nonce, now, GetAccountID());
  const auto data = native::challenge(nonce);
  EQApplicationPacket packet(OP_CitadelIntegrity, static_cast<uint32>(data.size()));
  std::copy(data.begin(), data.end(), packet.pBuffer);
  QueuePacket(&packet);
}

bool Client::HandleHailstoneIntegrityPacket(const EQApplicationPacket *app) {
  namespace native = bastion_native_integrity;
  if (is_player_zoning || !GetAccountID() || !hailstone_integrity.nonce()) return true;
  std::string allowed;
  auto configured = database.QueryDatabase(
      "SELECT `value` FROM `variables` WHERE `varname` = 'hailstone_bastion_pairs' LIMIT 1");
  if (configured.Success() && configured.RowCount()) {
    auto row = configured.begin();
    if (row[0]) allowed = row[0];
  }
  native::Report report;
  const auto bytes = std::span<const std::uint8_t>(app->pBuffer, app->size);
  const auto status = hailstone_integrity.evaluate(bytes, Timer::GetCurrentTime(), allowed, report, GetAccountID());
  const bool accepted = status == native::Status::Accepted;
  const char* reason = accepted ? "Native Bastion integration verified." :
      status == native::Status::Unsupported ? "This addon is missing required Bastion features." :
      status == native::Status::Malformed ? "Invalid native integration report." :
      "Native build pair is unapproved or its challenge expired.";
  // Echo the request nonce so a stale rejection cannot retire a newer report.
  const auto request_nonce = bytes.size() >= 24 ? native::read(bytes, 16, 8) : 0;
  const auto data = native::acknowledgement(request_nonce, status, reason);
  EQApplicationPacket reply(OP_CitadelIntegrity, static_cast<uint32>(data.size()));
  std::copy(data.begin(), data.end(), reply.pBuffer);
  QueuePacket(&reply);
  if (accepted) {
    LogChecksumVerification(
        "Hailstone native integration account [{}] version [{}] features [{}] executable [{}] bytes [{}] FNV63 [{}] addon [{}] bytes [{}] FNV63 [{}] approved [true]",
        GetAccountID(), report.addon_version, report.features, native::hex(report.executable.sha),
        report.executable.bytes, report.executable.fnv63, native::hex(report.addon.sha),
        report.addon.bytes, report.addon.fnv63);
    citadel_dll_passed = true;
    RefreshCitadelIntegrityState();
  } else {
    LogChecksumVerification("Hailstone native integration account [{}] rejected [{}]", GetAccountID(), reason);
    if (RuleB(World, EnableChecksumVerification) && !HasCitadelIntegrityBypass())
      return FailCitadelIntegrity("native Bastion integration report rejected");
  }
  return true;
}

'''
cpp = replace_once(cpp, 'bool Client::ChecksumMatchesVariable(uint64 checksum, const char* variable_name, const char* log_name) {',
                  methods + 'bool Client::ChecksumMatchesVariable(uint64 checksum, const char* variable_name, const char* log_name) {')
header = (source / 'world/client.h').read_text(encoding='utf-8')
header = replace_once(header, '#include <string>', '#include <string>\n#include "../common/hailstone_native_integrity.h"')
header = replace_once(header, '\tbool citadel_integrity_failed;',
                      '\tbastion_native_integrity::Session hailstone_integrity;\n\tbool citadel_integrity_failed;')
header = replace_once(header, '\tbool HandleCitadelIntegrityPacket(const EQApplicationPacket *app);',
                      '\tbool HandleCitadelIntegrityPacket(const EQApplicationPacket *app);\n\tvoid SendHailstoneIntegrityChallenge();\n\tbool HandleHailstoneIntegrityPacket(const EQApplicationPacket *app);')
files = {'world/client.cpp': cpp, 'world/client.h': header,
         'common/hailstone_native_integrity.h': (root / 'server/native_integrity.h').read_text(encoding='utf-8')}
for name, text in files.items():
    before_path = source / name
    before = before_path.read_text(encoding='utf-8') if before_path.exists() else ''
    hashes[name] = hashlib.sha256(before_path.read_bytes()).hexdigest() if before_path.exists() else None
    destination = output / name
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(text, encoding='utf-8', newline='\n')
    patch.extend(difflib.unified_diff(before.splitlines(keepends=True), text.splitlines(keepends=True),
                                    fromfile='a/' + name if before_path.exists() else '/dev/null', tofile='b/' + name))
(output / 'hailstone-native-integrity.patch').write_text(''.join(patch), encoding='utf-8', newline='\n')
(output / 'base-sha256.json').write_text(json.dumps(hashes, indent=2) + '\n', encoding='utf-8')
print(f'Prepared {len(files)} files and review patch in {output}; server source was not changed.')
