# Bastion parity review

The implementation was checked against the owned Citadel DLL and server/quest source, reviewed
Hailstone upstream 9263810d, and the earlier unfinished managed-client integration. That older
integration was reference material; the standalone addon does not link it or require a managed build.

## Material gaps found and addressed

- **Tracking maps:** the earlier port omitted Citadel's 0.2 multiplier, used 3D rather than planar
  distance, missed merchant classes 67/68/70, and named unrelated players. This port corrects these
  differences and claims exclusive extension actor markers through a generic host policy.
  Bastion disables the stock NPC map/minimap overlays and NPC Find, including selected/hovered
  marks, cached lists, quest discovery and target/path/walk actions. Ownership remains active
  during zoning. Both an existing Find trail and a Find-owned NPC walk stop when blocked or
  ineligible; manual movement and player navigation remain available. Terrain, static labels,
  personal pins and saved stock preferences are preserved, and other servers regain stock behavior.
  Trained ranger (4), druid (6) and bard (8) characters use skill 53 values 1–252, memorized tracking techniques,
  and `floor((200 + skill*20 + technique)*0.2)`. Received GM state retains its Bastion marker
  exceptions but does not re-enable stock NPC Find. The map-local Filters control mirrors the DLL's
  independent NPC, merchant, other-player, corpse and raid-name preferences. Group members are on
  by default, with a new explicit display-only opt-out; self and group Find/navigation remain available.
  Explicit category switches also hide GM markers, so the controls remain meaningful while GM
  eligibility and range exceptions apply only to enabled categories. These display preferences do not
  change the player's saved stock group-map setting for other servers.
- **Raid bars:** the earlier draft missed real group headings/placeholders, leader ordering,
  repeated-click pet targeting, resizing, class colors, management and much of the command syntax.
  The native Lua rewrite implements these, adds context/controller alternatives, out-of-raid
  configuration, observed resources and overflow handling, and avoids stale actor targeting.
  Group layout retains self and ignores class filtering by default; class layout uses the filters.
  Both the stock Raid window and the Raid Bars Manage page include Delegate Raid Manager
  List/Grant/Revoke requests. Stock controls use the modular UI API's owned action extension point,
  work with the raid-bar overlay hidden, and retire across zoning or permission loss. Grant and revoke
  review the current member and roster revision; the server confirms results in chat and enforces
  actual raid leadership. No local delegate status is inferred from a sent request.
  The actual DLL's HP threshold is strictly below the threshold. Unsupported font selection was
  never a working DLL feature and is not claimed as a required missing feature.
- **DZ manager:** delegate and other server command arguments are preserved rather than silently
  consumed by the saved-instance window. Rejoin remains explicit and server-authorized.
- **Factions:** partial-name commands, global search, category behavior, retained viewport,
  numeric sorting and file export are preserved.
- **Pet gear:** cursor drag/drop, inspection and reviewed/controller actions coexist. Removal
  cooldown does not incorrectly prevent equipping. Received snapshots remain authoritative.
- **Merchant selling:** uses the actual private 20-byte payload rather than substituting a normal
  merchant quantity dialog. Whole stacks, worn/carried slots and bag slot mapping are supported;
  held cursor, Keep and native sale restrictions consume the click without using the item.
- **Metadata:** Kinbound and item/augment ratings compose with common item displays and previews;
  revision changes invalidate cached text. Nested items receive Kinbound loot warnings.
- **Home city:** numeric city IDs preselect only a matching received choice, then require explicit
  review/confirmation; they never silently select another choice or commit on command execution.
- **Firefell:** zone 406, current instance identity and FFZ1 nonce are required. Public version 249
  and private versions 250–255 receive their current display names without changing the zone catalog.
- **Command inventory:** all 187 static native command registrations and 18 quest registrations
  from the inspected source are represented. Database aliases and access overrides remain runtime
  concerns, so the catalog directs players to server `#help` for current availability.
- **Native integration checks:** the legacy server recognizes only a Citadel DLL digest. The new
  negotiated CIT1 version 3 path reports the actual Hailstone executable and loaded addon digests,
  verifies an approved SHA256 pair, and acknowledges acceptance explicitly. The companion server
  patch keeps existing game-file checks and legacy handling. See [the integration guide](INTEGRITY.md).

## Presentation changes

Telegraphs share a coral/blue/gold/mint role palette, fine white semantic glyphs, restrained boundaries,
compact impact clocks and restrained urgency pulses. Server-authored colors remain an explicit option. Required
markers preserve their opacity floor, terrain projection and complete-presentation proof rules.
All tools use the Modern/Gamepad conventions and a shared theme. `/bastion` separates tools,
settings and searchable command help without starting movement or sending a command on open.

Ground placement uses the renderer's selected smoothed/refined terrain and replacement props, plus
current dynamic and animated visual surfaces. It leaves movement collision unchanged. Native surface
patch proofs and real face boundaries handle small off-grid props, slopes and successive stair treads;
uncertain geometry or exhausted budgets defer the marker rather than certify an inaccurate surface.
Decorative cutout textures retain a mesh-footprint limitation; actor bodies are not ground surfaces.

## Remaining external acceptance gaps

- The performance review fixes reproduced large-raid map scaling and unnecessary hidden-window
  work. The reported two-member live Inventory timeout has not reproduced locally; it still needs
  a matching hitch capture and gameplay retest. See [measured costs and limits](PERFORMANCE.md).
- The prepared version 3 server change and exact binary-pair approval need deployment, followed by
  ordinary-account admission testing. Local protocol tests do not establish live server acceptance.
- The inspected server supplies raidwide HP but subgroup-scoped mana/endurance. The client cannot
  manufacture remote observations; unknown resources stay unknown.
- The DLL's `#raidmove`/`#raidpromote` request paths are retained for explicit delegated management
  where native operations are unavailable. Matching deployed handlers and permissions need live
  verification; issuing a request is never reported as a successful roster change.
- Login/zone transitions, live protocol negotiation, actual item updates, slopes/lifts and encounter
  timing, 72-member raids and physical controller use still need gameplay acceptance.
- Map restrictions are consistent UI policy, not a security boundary over server-sent spawn data.

Sentinel, nameplates and spell ceilings are intentionally provided elsewhere. Item Search is not
included. Ordinary achievements/rewards, expedition information, tribute and target buffs reuse
Hailstone's existing systems rather than introducing duplicate server implementations.
