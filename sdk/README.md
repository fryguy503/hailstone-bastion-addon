# Public Hailstone SDK snapshot

These MIT-licensed headers are based on upstream Hailstone commit
`6446875a5e01037aac565776d9e0f8f6f97093da`. `hailstone_plugin.h` also includes the
companion client's later class-name tail slot and generic popup/camping services.

`hailstone_plugin.h` SHA256
`082b168e699eba611095ebc277873d1d7aa21dec1e48a94e177e40058bbc3582` (LF line endings).

The addon uses `HAILSTONE_PLUGIN` to export the public ABI 3 entry points. It uses
`HAILSTONE_HOST_HAS` before calling optional host services. Raid observations come from
`get_raid_data_bulk`, and raid targeting and moves use the public host calls.

Map ownership requires the extended map-policy record and a nonzero host-issued world context.
The addon checks for a usable context before activating on a selected Bastion world.

Preview 6 checks the optional `get_large_dialog` and `answer_large_dialog` services before
offering fast camp. These use current popup/camp tokens; `HAILSTONE_PLAYER_CAMPING` reports
native camping without changing the existing player-record layout. The scheduler reads
`Spell.DurationFormula` and `Spell.DurationCap` for ordinary multi-song rotations; when those
raw members are absent, it pauses with an update message rather than guessing long-song timing.
Single-gem, hold and one-shot controls can still run without those members. Existing `Spell.Duration`
and all previously released host-table offsets remain unchanged.

The companion client source validated with Preview 6 is
`074401a11bde6d6c06ea92aecf11492bbf39c438`.
