# Public Hailstone SDK snapshot

These MIT-licensed headers are based on Hailstone's public SDK. Preview 11 uses
the canonical ABI 4 table with the ground placement services. The add-on checks the placement
capability before calling into the host, so earlier ABI 4 layouts can keep their other features.

The addon uses `HAILSTONE_PLUGIN` to export the public ABI entry points. It uses
`HAILSTONE_HOST_HAS` before calling optional host services. Raid observations come from
`get_raid_data_bulk`, and raid targeting and moves use the public host calls.

When a host provides the optional bounded surface extension, telegraphs can reserve
native terrain work for other warnings. The optional regional surface revision lets
telegraphs reuse certified geometry across scenery animation outside the sampled
region. SDKs and hosts without those extensions use the original surface patch callback
and scene revision.

Map ownership requires the extended map-policy record and a nonzero host-issued world context.
The addon checks for a usable context before activating on a selected Bastion world.
The provider uses the 96-byte map-policy record for server-only NPC Find. Companion builds
can use the 104-byte record to enable visual guides for the current target and Bazaar traders. The legacy
88-byte record keeps NPC Find disabled and uses Bastion's marker policy.

Fast camp checks the optional `get_large_dialog` and `answer_large_dialog` services where
the companion SDK supplies them. The canonical SDK build uses the ordinary camp path.
The scheduler reads
`Spell.DurationFormula` and `Spell.DurationCap` for ordinary multi-song rotations; when those
raw members are absent, it pauses with an update message rather than guessing long-song timing.
Single-gem, hold and one-shot controls can run on clients that lack those members.
