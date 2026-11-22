# Public Hailstone SDK snapshot

These MIT-licensed headers are based on Hailstone's public SDK. Preview 7 refreshes
`hailstone_plugin.h` and its nameplate and UI contribution dependencies from client revision
`eca9cf1de5cf4cee9e50673281c4c93b034e499a`, with the optional actor-guide field.

`hailstone_plugin.h` SHA256
`2094d6304abd919551550a5b50c376bfa50c4b12871a8838819eda175c685d96` (LF line endings).

The addon uses `HAILSTONE_PLUGIN` to export the public ABI 3 entry points. It uses
`HAILSTONE_HOST_HAS` before calling optional host services. Raid observations come from
`get_raid_data_bulk`, and raid targeting and moves use the public host calls.

Map ownership requires the extended map-policy record and a nonzero host-issued world context.
The addon checks for a usable context before activating on a selected Bastion world.
The provider uses the 96-byte map-policy record for server-only NPC Find. It requires the 104-byte
record to enable visual guides for the current target and selected Bazaar traders. The legacy
88-byte record keeps NPC Find disabled and uses Bastion's marker policy.

Fast camp checks the optional `get_large_dialog` and `answer_large_dialog` services in the current
host-table layout. These use current popup/camp tokens. `HAILSTONE_PLAYER_CAMPING` reports
native camping without changing the existing player-record layout. The scheduler reads
`Spell.DurationFormula` and `Spell.DurationCap` for ordinary multi-song rotations; when those
raw members are absent, it pauses with an update message rather than guessing long-song timing.
Single-gem, hold and one-shot controls can run on clients that lack those members.
