# Public Hailstone SDK snapshot

These MIT-licensed headers come from upstream Hailstone commit
`6446875a5e01037aac565776d9e0f8f6f97093da`.

`hailstone_plugin.h` SHA256
`4ca869d5b902ad823787ac9a0426d07a68d580653f9a27d324eda8c782f74cc6` (LF line endings).

The addon uses `HAILSTONE_PLUGIN` to export the public ABI 3 entry points. It uses
`HAILSTONE_HOST_HAS` before calling optional host services. Raid observations come from
`get_raid_data_bulk`, and raid targeting and moves use the public host calls.

Map ownership requires the extended map-policy record and a nonzero host-issued world context.
The addon checks for a usable context before activating on a selected Bastion world.
