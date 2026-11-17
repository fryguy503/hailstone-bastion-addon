# Performance review — 2026-11-17

This review responds to map open/close freezes, an Inventory callback stopped after 5,000 ms,
and the raid overlay's missing `HorizontalScrollbar` flag. The live report used a two-member raid.
The large-raid map regression below is reproduced; the particular live Inventory timeout is not.

## Changes

- Map membership keeps a bounded, sorted snapshot of at most 72 `(spawn ID, incarnation)` pairs.
  Each lookup still reads the current host roster header. Changed revision, owner, character,
  zone, instance, failed/partial reads or lifecycle changes retire the snapshot. A second header
  check prevents publishing a roster that changed during enumeration. Actions and vitals never
  use this display cache.
- Closed hub, faction, pet, home-city and expedition windows request visibility only. These
  queries do not build catalog/slot/choice arrays or query the game, cursor, clock or filesystem.
  Automatic home-city reminders remain in the normal pulse path.
- A disabled raid overlay requests a small status record, skipping roster, spawn and pet scans.
  Opening settings or an enabled overlay requests fresh full state. Out-of-raid settings remain
  available while in the world; local zoning retires the overlay even if the host player snapshot
  briefly still reports the previous zone.
- Character directories are resolved once per live scope instead of invoking the host's
  directory-creating filesystem helper on every frame. The scope includes the opaque host
  connection context, character/server identity and zone. Reconnect, peer, host and zone changes
  invalidate it. Missing permissions/context/path clear the old destination; explicit saves
  recreate a deleted directory. Hosts without scope readers retain uncached compatibility.
- Previously opened panels clear their local open/review state when their provider disappears
  or leaves the active Bastion session. They stop full-state polling and require an explicit
  reopen after a later session.
- Raid manager labels use List / Grant / Revoke ordering. Reviewed and received-tool action rows
  wrap at measured text widths, accounting for both packs' frame padding and differing UI scales.
  The optional scrollbar flag falls back to zero on hosts that do not export it.
- Narrow received-tool browsers stack their panes within the existing height. Home-city details
  become compact lines and the full-width choice list replaces the narrow atlas; wider windows
  retain the atlas and profile table.

No Lua timeout, memory limit, garbage-collection pacing, world-geometry budget, SDK layout,
server protocol, movement behavior or action permission check was relaxed.

## Reproduction and attribution

The baseline is addon `155cdda`, DLL SHA256
`c8564312cc323d5025eb5c1aeec0397c42b08da89dbf6e1fd25c2b9ced0a3db9`,
with client `ebc7878a`. A native fixture uses the actual plugin loader, Lua UI/API, ImGui,
mechanics Character, 33 possessions and 100 bag items. It loads all 192 stock and 10 addon
callbacks, with 400 synthetic actors and either 2 or 72 raid members. It preserves native
callback limits and GC, observes complete errors and uses no real player or server actions.
CPU-side wall timings exclude GPU submission/readback and can include scheduling delays;
they are workload evidence, not a frame-rate guarantee.

Map painting can ask visibility several times per actor. Previously, each query enumerated all
raid members, and each host member read revalidated the entire roster. The resulting
`actors × raid members²` work explains the reproduced large-raid stall.

An isolated build changing only the map cache, with otherwise unchanged baseline Lua, measured:

| Workload | Baseline median | Map cache median |
|---|---:|---:|
| 72 raid / 400 actors, Map | 1,150.525 ms | 21.788 ms |
| 72 raid / 400 actors, Minimap | 1,283.900 ms | 17.586 ms |
| 2 raid / 400 actors, Map | 3.599 ms | 2.429 ms |
| 2 raid / 400 actors, Minimap | 2.711 ms | 2.179 ms |

These are isolated map-cache results, not the final combined build. Inventory stayed below
1 ms in this fixture. The old overlay error was deliberately retained for this attribution run.

## Combined candidate

The final addon is `0.1.0-preview.2`, DLL SHA256
`7746e58838b4e9eba1ce760560e21446e856190cb6ed155e8b3c2c411502c6e1`.
Sixteen native cases ran against the frozen `a8a27532` client with an explicit synthetic login
profile, server/world/character identity and isolated writable character directory. Each loaded
case required a successful native setting write and verified the resulting `raidbars.cfg` before
timing. The earlier exploratory r4 fixture lacked that complete identity and is not the basis for
settings-path or enabled-overlay acceptance. Compile and visual workers were idle during this
final timing batch; stock and published-addon controls used the same profile initialization.

| Two-member raid / 400 actors, warm median | Published addon | Final addon | Stock control |
|---|---:|---:|---:|
| Modern Lua frame | 9.115 ms | 3.574 ms | 1.425 ms |
| Gamepad Lua frame | 5.769 ms | 2.962 ms | 0.911 ms |
| Modern open Inventory | 0.532 ms | 0.334 ms | 0.297 ms |
| Gamepad open Inventory | 0.313 ms | 0.263 ms | 0.263 ms |
| Modern open Map | 3.289 ms | 2.383 ms | 0.981 ms |
| Gamepad open Map | 2.649 ms | 2.144 ms | 0.232 ms |

All 16 cases completed with zero Lua callback/core errors. Both packs received 400 faction entries,
12 saved expeditions, 16 home choices and pet status through real inbound hooks. Ratings independently
reported ready state, 33 templates and a received summary. Pet item payloads were not seeded.
The separate hidden-window observer recorded visibility/status calls and zero full-state calls;
it did not trace Windows filesystem calls.

Enabled raid bars were separately verified with accepted settings, 72 actual member rows, changing
known HP, and then 18 owner-linked pets. Native window vertices and exactly 72/90 calls to the
original native progress-bar binding established that the overlay really drew. Twenty warm draws
per setting measured the Raid Bars callback at 1.327/1.570 ms Modern and 1.454/1.741 ms Gamepad
without/with pets. Whole-UI medians were 20.315/19.180 ms, including the remaining large-raid
minimap cost. No action or delegate request was submitted.

Each pack and its stock control then completed 20 warmup plus 1,000 measured frames, toggling
Inventory and Map. Final Inventory p50/p95/max was 0.359/0.555/0.707 ms Modern and
0.289/0.453/0.605 ms Gamepad. Final whole-Lua p50/p95/max was 5.690/9.183/131.481 ms Modern
and 4.878/7.938/20.686 ms Gamepad. Lua heaps collected below their peaks in both packs.

The 131.481 ms Modern frame is an unresolved outlier: its 202 measured window callbacks summed
9.166 ms, Inventory took 0.370 ms, GC 0.0017 ms and separately timed OnPulse 0.0193 ms. The next
frame also had unattributed non-window time. Scan/reload/settings phase durations were not retained,
so the cause cannot be assigned from these records. No five-second timeout occurred.

Full JSON/CSV/log/compiler-input evidence is sealed locally. Final report SHA256:
`88df6249ebb3dc9d647491f3553c9719aa960b810c1fb1789760d61f5b0929f4`;
133-file evidence-manifest SHA256:
`dd3dee9e6105f70b8e019b90c9e6e904037fca32d379c5458097b1bf4243a880`.
Narrow UI and GPU presentation use a separate native capture matrix: all 30 captures and root
window bounds pass in both packs. Home-city/faction before-and-after views, the actual stock Raid
controls, hub/settings and map filters were inspected. Captures also exposed the shared Gamepad
Raid/Map viewport overflow; the client helper now clamps those windows while retaining the wider
geometry. The final UI-only client source is `a8d9c741`, using the unchanged validated C++ runtime
`a8a27532`. All 279 packaged UI files match the captured files byte-for-byte. The self-contained
gallery SHA256 is `90b07276f53b799531bb4a8f636243a29793b80071b7bfd832f1a48334191f8b`.

## Regression coverage and limits

The Windows candidate passes all 31 CTest suites with strict MSVC warnings as errors. Linux passes
all 19 native suites with warnings as errors, AddressSanitizer, UndefinedBehaviorSanitizer and
leak detection. The 12 Lua suites run on Windows against the immutable matching native pack helpers.
Tests cover unchanged-directory call counts, same-name/different-profile sessions, failed scope
switches without writes to the old character, reentry, truncated context records, display cache
retirement, hidden-panel work counts, reopening, narrow action rows and both scrollbar exports.

The unchanged baseline also completed 1,000 measured frames after 20 warmup frames, loaded and
unloaded. Loaded Lua frame p50/p95/max was 9.139/13.365/37.853 ms; Inventory was
0.551/0.658/0.863 ms. Both modes collected growing Lua heaps; the run did not demonstrate an
addon-specific unbounded heap increase. The final fixture omits owned AAs, buffs, real item
textures, real pet equipment and live network updates. Its complete profile identity/settings
paths do not recreate the user's complete live character or assets.

The remaining map cost includes the host revalidating the roster on every header read. A separate
client draft, `raid-roster-epochs-draft`, adds authoritative roster/actor identity revisions and
linear rebuilds. Source and syntax reviews found no blocker; linked-engine/runtime validation
is still pending, and the draft is excluded from this preview. Replacing freshness with a timer
would weaken immediate membership removal and identity checks.

The original two-member live Inventory timeout still needs a matching hitch capture and gameplay
retest. Native load tests and sanitizer results do not establish zero gameplay impact, hours-long
stability, real encounter performance, physical controller behavior or server acceptance of a new
executable/addon integrity pair.
