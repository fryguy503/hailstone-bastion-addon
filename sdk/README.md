# Public Hailstone SDK snapshot

These MIT-licensed headers are based on Hailstone's public SDK. Preview 8 uses
the ABI 3 table from client revision `5ffcaccafd2bb119875f4237b9124f58571e7813`
and includes the optional bounded surface patch and regional surface reuse extensions.

`hailstone_plugin.h` SHA256
`b2108fec021f678d9e32803e41bf8a8a690df466df109cd20b545e79f252116e` (LF line endings).

The addon uses `HAILSTONE_PLUGIN` to export the public ABI 3 entry points. It uses
`HAILSTONE_HOST_HAS` before calling optional host services. Raid observations come from
`get_raid_data_bulk`, and raid targeting and moves use the public host calls.

When a host provides the optional bounded surface extension, telegraphs can reserve
native terrain work for other warnings. The optional regional surface revision lets
telegraphs reuse certified geometry across scenery animation outside the sampled
region. Older hosts use the original surface patch callback and scene revision.

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
