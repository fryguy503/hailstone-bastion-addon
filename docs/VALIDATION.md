# Preview validation — 2026-11-17

## Preview 6 song twisting and fast camp — 2026-11-19

| Check | Result |
|---|---|
| Windows x64 MSVC addon | 34/34 CTest suites passed, warning-free |
| Linux addon, ASan/UBSan | 22/22 suites passed; no sanitizer findings |
| Citadel twist scheduler | 645 focused checks, including strict GCC sanitizer/leak coverage |
| Server-offered fast camp | 195 focused Windows checks, compiled with `/W4 /WX` |
| Both UI packs with native client helpers | Hub suite passed 7,055 checks; all 12 UI suites passed |
| Actual native loader, spell commands, casting gauge and loopback transport | Two CastSpell receipts in gem-slot order 0, 1; active manual cast protected; `/stoptwist` stopped the scheduler |
| Actual native server-offered camp | Default-off and unrelated offers ignored; exactly one `{0, 2000000002}` popup response; zero early Logout packets; received server disconnect completed cleanly with destination 2 |
| Companion client, Windows MSVC with browser off | Full suite: 3,977 cases, 1,728,702 assertions, 74 optional skips; focused API/network checks: 48 cases and 5,522 assertions |

The schedulers cover gem ordering/duplicates, once/hold/resume, historical durations, recast
timing, missed notes, manual-cast protection, permissions, reentrant observations and cached
character settings. WORLD zoning preserves a same-character passive twist queue without
starting it; other character/server identities clear it. Fast camp checks exact offer IDs,
native camp/dialog tokens, actor incarnation and opaque connection context. Existing camps,
stale/replaced offers, cancellation, expiry and missing services keep the normal camp path.
Lifecycle changes also clear old status/error copy from both Settings sections.
Camp transport tests use the host's existing `game.fast_camp` option at its default off value;
the addon leaves a player's separately enabled global option unchanged.

The native fixture uses the real plugin loader and SDK table, MQ registry, stock casting gauge,
GameConnection and SOE transport with isolated generated character/spell records. Popup tokens
retire on zone/connection changes; a validated current-session server disconnect completes the
accepted camp, while malformed packets, timeouts and socket loss remain separate. The measured
loaded DLL matches the release digest below. No GPU or live player profile is involved.
Incoming cast/popup records use the native packet-test entry and actual decoders; outgoing
casts, Camp and PopupResponse plus the final server disconnect use real loopback UDP.
The tested companion client source is `074401a11bde6d6c06ea92aecf11492bbf39c438`.
Optional asset/vector cases remain skipped when their external inputs are absent.

Inactive schedulers perform zero host observations; active polls are bounded to 100 ms.
These checks do not measure live raid frame times. Live Bastion song behavior, server zone
eligibility and controller interaction still need gameplay acceptance. No production server
configuration or database was changed.

The Windows DLL is 876,032 bytes, SHA256
`5a486fc92dd07539fee018b84808e756c1c16c7b36cc006dfd34a34e037bb9b0`.
Its measured legacy `crc_dll` value is `7645740817213870030`; the release includes review-only
append approval SQL and JSON measurements. A fresh approved login remains necessary.

## Preview 5 login and zoning correction — 2026-11-18

| Check | Result |
|---|---|
| Windows x64 addon | 32/32 CTest suites passed |
| Linux addon, ASan/UBSan | 20/20 suites passed; no sanitizer findings |
| Integrity lifecycle and SDK output | 949 fixture checks passed on Windows and Linux |
| Plugin routing and LoginInfo bounds | 145 checks passed |
| Both UI packs with current client helpers | Hub suite passed 6,472 checks |
| Real native loader, measurement, consent, GameConnection and loopback transport | Released Preview 4 reproduces ignored zoning submission; Preview 5 preserves the original receipt and sends no zoning report |

The transport comparison uses the exact released Preview 4 DLL, a frozen Preview 5 build, and the
production `ee744e4e` engine. Early approval sends one measured report before CRC1; approval at
character select sends before EnterWorld. Approval after zone entry remains **Reconnect required**,
including after a real server-requested WORLD zoning reconnect. Pending legacy/native work is
retired when the initial login ends, including reentrant close during measurement or send.

The inspected Bastion server deliberately ignores reports with `LoginInfo.zoning=1`. The test uses
synthetic server identity, approvals and loopback sockets; no live server database was read or written.
The companion generic client entry change holds manual, keyboard, automatic, boxed and Return Home
entry while native login approval is pending. It does not change the server protocol or ordinary zoning.
Live acceptance still requires approving the new DLL and performing a fresh full login, then checking
the server's `account.crc_dll` receipt. Server allowlist approval is separate from that receipt.

## Preview 4 legacy DLL integrity

The addon uses the existing CIT1 version 2 DLL slot with the measured loaded-addon FNV63 checksum.
It submits synchronously before a WORLD login checksum or EnterWorld, measures once per login,
retries the cached body at most three times, and gives a valid version 3 challenge precedence.
Legacy submission cannot display server acceptance because that protocol has no acknowledgement.

| Check | Result |
|---|---|
| Windows x64 MSVC, public upstream SDK snapshot | 32/32 CTest suites passed: 19 C++, 12 Lua and the approval-tool suite |
| Linux GCC, ASan/UBSan | 20/20 suites passed: 19 C++ and the approval-tool suite; no sanitizer findings |
| Integrity wire, retries, protocol preference and reentrant session resets | 672 fixture checks passed on both native builds |
| Plugin routing and late-login fallback | 91 fixture checks passed on Windows |
| Both UI packs with current Hailstone `55f668c0` helpers | All 12 Lua suites passed; hub suite includes 6,426 checks |
| Legacy checksum/approval tool | 12 Python tests passed on Windows and Linux |

The SDK remains the checked-in upstream `6446875a` snapshot. Packet tests use controlled host
measurements and send callbacks; they verify synchronous dispatch and preserve the original login
packet. The approval tool separately measures the built DLL and prepares append-only `crc_dll` SQL.
Its SQL predicate is exercised with SQLite compatibility functions, not a live MySQL database.

No Hailstone core rebuild, production-profile installation, server configuration change or live
login was performed for Preview 4. A real login with this DLL approved in `variables.crc_dll`
remains necessary to confirm deployed-server acceptance. Earlier GPU/performance evidence below
belongs to the stated earlier builds.

## Preview 3 on upstream Hailstone

Windows x64 MSVC passed all 31 add-on suites with warnings treated as errors. These include native
tests and all 12 Lua suites using the Modern and Gamepad files from upstream `6446875a`.

An offline smoke test linked against that commit's Windows engine and used the Bastion server policy.
The loader reproduced Preview 2's missing entry point error. It loaded Preview 3 with native approval
and recognized the required plugin both during world login and when first loaded in a synthetic zone.
The smoke test checked `/bastion`, both UI pack registrations, map ownership and the bulk raid roster.
Live login, gameplay and server integrity approval remain unverified for this build.

## Performance and UI correction

Preview 2 keeps the Canonical 2 SDK bytes/layout documented below and works through
the published Canonical 3 host's automatic typed compatibility view. No ABI override is required.

| Check | Result |
|---|---|
| Windows, strict MSVC warnings as errors | 31/31 CTest suites passed |
| Linux, strict warnings as errors / ASan / UBSan / leak detection | 19/19 native suites passed; no findings |
| Both UI packs with the immutable matching host helpers | All 12 Lua suites passed on Windows |
| Character-directory scope and failure regression | 547 checks passed |
| Raid overlay/settings and stock manager UI | 1,563 / 570 checks passed |
| Hidden received tools | 960 visibility queries per tool: zero game/cursor/clock/directory reads, arrays or requests |
| Native client/addon matrix with complete profile identity and settings paths | 16 cases, zero callback/core errors; real settings-path/file checks |
| Sustained native UI | Four completed 1,000-frame runs across both packs and stock controls |
| Enabled raid bars | Both packs: 72 members, then 18 pets; actual settings, vitals, vertices and 72/90 native bar calls verified |
| Matching a8 client runtime | Windows 3,766 cases / 1,680,419 assertions; Linux 3,772 / 1,680,866; 73 optional skips each |
| Matching runtime asset checks | Windows 154 / 3,895; Linux 290 / 758,227; no sanitizer/leak diagnostics |
| Native GPU layouts and viewport bounds | 30/30 captures passed; both packs, normal/narrow/short viewports; all original PNGs opaque |

The final UI-only client source is `a8d9c741`, paired with the unchanged validated `a8a27532`
C++ executable/engine. The shared Gamepad window helper clamps Raid/Map to the viewport;
all 279 packaged UI files match the captured inputs. All 31 addon suites also pass against
that corrected helper. The local preview carries the exact runtime/UI provenance separately.

The [performance review](PERFORMANCE.md) distinguishes the reproduced large-raid map stall,
isolated measurements, combined candidate evidence and the unresolved two-member live Inventory
timeout. These checks do not establish live gameplay or server binary-pair acceptance.
The fixture does not include the player's owned AAs, buffs, real item textures or real pet gear;
profile completeness here refers to login identity and writable settings paths.

## Earlier preview validation — 2026-11-16

That preview targets Canonical 2 SDK commit `c55bfccd`, tree `166baf23`, on upstream `843b6e46`.
Header SHA256 is `673a348cad651775ae74741a718cd5c3f0230a035e1cafa3eaabe640aa4eb9d6`;
layout `0xe7faba7b569803ce` has 106 members, 103 function slots and 840 bytes on Windows x64.
The released raid-bulk field precedes the addon services. The prior marked F6 view remains separate.
No release or server deployment has been made.

## Canonical 2 map filter build

| Check | Result |
|---|---|
| Windows addon, strict MSVC warnings | 30/30 CTest suites passed |
| Binding and compatibility regression | 80 checks passed, including explicit prior F6-tag refusal |
| Filter/provider backend | 190 C++ fixture checks passed |
| Map filter UI / shared hub UI | 1,139 / 5,728 checks passed |
| Linux addon sanitizers | 30/30 suites passed (18 C++ / 12 Lua); strict warnings, ASan/UBSan/leaks clean |
| Current-layout native client/DLL integration | 2,166 checks across all six pack/role cases passed |
| Marked F6 DLL on current host | 361 checks passed through the automatic typed 832-byte view |
| Current E7 DLL on genuine old F6 loader | 16 checks passed; refused before binding/InitializePlugin |
| Native GPU captures | 35 PNGs / 40,320,000 fully opaque pixels passed audit |
| Full matching Windows client | 3,702 cases / 1,671,370 assertions passed; 73 marked optional skips |
| Windows focused / asset-backed checks | 212 cases / 4,618 assertions and 147 cases / 3,840 assertions passed |
| Full matching Linux client | 3,708 cases / 1,671,816 assertions passed; 73 marked optional skips; no sanitizer or leak reports |
| Linux focused / asset-backed checks | 265 cases / 723,855 assertions and 283 cases / 758,172 assertions passed |

Both matching client full suites and asset-backed checks passed on the frozen source; the Linux
run independently verified 2,257 source files afterward. Platform-focused selections differ.

The map UI and provider logic are unchanged by this SDK rebuild. Fresh current-layout and cross-layout
checks pass; the earlier F6 runs below remain preserved separately. The reverse compatibility probe
links the sealed old F6 engine and libraries, then calls its actual native loader with the new DLL.
It does not reject the DLL through a preflight identity check. Operating-system module loading and
CRT/static constructors can run before the loader refuses host binding and initialization.

## Earlier F6 map-filter validation

The following results belong to the earlier 832-byte SDK and its tested client/DLL pair.

The earlier map-filter build used Canonical 1 (ABI 3, layout `0xf6b34b37061be009`) with
`hs.UI` 1.5 client `a1426cdb`, tree `3efda860`, based on upstream `0e1fe786`. Its actual map
integration and both full client suites passed before the next released SDK field was integrated.
No release or server deployment was made.

## Map filter update

| Check | Result |
|---|---|
| Windows addon, strict MSVC warnings | 30/30 CTest suites passed |
| Linux addon, ASan/UBSan and leak detection | 18 C++ and 12 Lua suites passed; strict warnings as errors; no sanitizer findings |
| Filter/provider backend, C++ fixtures | 190 checks passed |
| Isolated map filter UI, both packs | 1,139 checks passed |
| Shared Bastion hub UI | 5,728 checks passed |
| Final client, actual map panel and GPU integration | 722 checks across Modern tracker and Gamepad nontracker; 10 fully opaque captures passed |
| Prior six pack/role native matrix | 2,166 checks and 30 opaque captures passed on 71c1; C++ implementation and addon inputs unchanged afterward |
| Full shared Windows client | 3,686 cases / 1,670,767 assertions passed |
| Full shared Linux client | 3,692 cases / 1,671,217 assertions passed; no sanitizer or leak reports |

The standalone addon runs use immutable 71c1 Windows and 77a Linux SDK/helper inputs; the tested
addon production/test files remain unchanged. The final actual-client rerun covers both changed
core helpers without repeating unchanged DLL/unit tests. The first Linux addon compile attempt
caught a misleading-indentation warning in a new test helper, before any tests ran. The formatting-only fix passed a fresh frozen run without
weakening warnings or sanitizers. Current production and test files match that successful snapshot.

The addon shares six checkbox rows between the map panel and Settings. Group members and raid
names are available without Tracking; NPCs, merchants, other players and corpses retain native
tracking/GM admission. Group filtering affects markers, preserving self and PC Find/navigation.
Explicit category choices apply to GM markers too. Source/context checks and native guards reject
stale or inactive-peer edits. Map preference changes advance a content revision without reopening
the hub or changing saved stock group-map preferences.

The actual-client smoke clicks the real Map controls opener and Group members checkbox, using
observation-only wrappers around unchanged native ImGui widgets. It verifies marker and hover
suppression across full-map, DrawZone and minimap paths, then restores them. A connected mock
server supplies an already-active PC Find path and a ready manual-navigation fallback lease;
both survive a display-filter change. Self, geometry, pins and the saved stock ShowGroup value remain.
Painting and filter edits send no captured SDK packet or server command. This is offline synthetic
input/game data, not live server or physical-controller acceptance.

The full 71c1 matrix covered tracker, nontracker and GM roles in both packs. The a142 change only
gates component calls before falling back on older UI APIs; its API 1.5/C++/SDK behavior is unchanged.
The final two-case rerun uses the new exact engine, executable and fresh packaged helpers. Its
11,520,000 captured pixels are fully opaque. Earlier candidate files and captures remain preserved.

## Earlier canonical SDK preview (77a)

The following results describe the preserved earlier canonical client/addon pair, before map-local
filter controls were added. They remain historical evidence, not acceptance of the new map panel.

That preview targets canonical SDK commit `77a9e4e4977911636f6acfecb479347e7d7e9676`, tree
`1faf2f9a`. The header SHA256 is `a55e8e01fe24dd461ec0a831f88e31794cc31e03b1068b3f9461ecc576895c3a`.
ABI 3 layout `0xf6b34b37061be009` has 105 table members, including 102 function pointers, and is
832 bytes on Windows x64. The addon manually exports the layout and versioned host-binding functions,
with no unversioned setter. The matching canonical client passes its full Windows/Linux suites and
asset-backed checks. Its preserved executable/addon measurements and review-only approval SQL
identify that earlier pair; they do not approve the map-filter pair. No release or server deployment was made.

## Canonical SDK migration

| Check | Result |
|---|---|
| Windows canonical addon | 29/29 CTest checks passed |
| Linux canonical addon, ASan/UBSan | 18/18 C++ and 11/11 Lua suites passed with strict warnings as errors and leak detection; no sanitizer findings |
| Addon binding and compatibility regressions | 78 checks passed |
| Canonical addon DLL | SHA256 `7ce480d908a06f879f11a7ad0842a534f4a99b0a70ce778f377ba79dcd692f03`; separate from the preserved C5 pair |
| Actual canonical DLL/native map integration | 588 checks in six pack/role cases; 12 PNGs and 48 pixel/alpha checks passed |
| Explicit old-preview adapter | 93 checks passed for the exact old DLL hash |
| Marked-DLL adapter refusal | 17 checks passed |
| Actual old C5 loader | Refused the new DLL before host binding/InitializePlugin |
| Full Windows canonical client | 3,647 cases / 1,634,109 assertions passed; 73 marked optional skips |
| Full Linux canonical client, ASan/UBSan | 3,653 cases / 1,634,560 assertions passed; 73 marked optional skips; no sanitizer findings |
| Linux asset-backed canonical client | 212 cases / 65,783 assertions passed |
| Live server and physical controller acceptance | Not performed |

The new DLL uses `hailstone_plugin_layout` and `hailstone_plugin_set_host_v2`. The actual preserved C5
loader refuses it at the mandatory legacy-setter lookup; source order confirms rejection before host binding
or `InitializePlugin`.
Operating-system library loading can still execute static constructors first. An older preview DLL on the new
host requires an explicit `ui-preview-1` override for its exact SHA256; ABI 3 alone is insufficient.
The canonical native smoke uses the actual DLL with capability mask 2047 and fresh Modern/Gamepad
scripts. It verifies WORLD-only activation with no player before zone-in, tracker/nontracker/GM map
ownership, stock NPC/Find suppression, stale-token refusal and saved-preference restoration on another
peer. Rendering sends no command or raw addon packet. The legacy adapter check supplies an exact-digest
resolver to the real loader; it does not parse a player configuration file. No ambiguous unmarked DLL is
loaded with the canonical default. Earlier telegraph/TAA/performance results below remain historical;
those unchanged addon paths were not rebenchmarked for this SDK migration.

## Earlier context-aware map preview (C5)

The following results belong to preserved client commit `c5def65b`, directly on upstream `977b0743`,
and its previous addon DLL. They are historical validation, not acceptance of the canonical SDK pair.

| Check | Result |
|---|---|
| Windows addon with companion SDK and native pack helpers | 29/29 CTest checks passed |
| Linux addon, ASan/UBSan | 18/18 C++ and 11/11 Lua suites passed with strict warnings as errors; no sanitizer findings |
| Addon host compatibility | 66 checks passed, including prefix-only readers that preserve the caller's buffer size |
| Map/minimap/Find presenter regression | 1,882 checks passed over the actual Modern and Gamepad scripts |
| Actual DLL, native host and GPU presenters | 534 checks passed across six pack/role cases; 12 opaque native screenshots independently decoded |
| Full Windows client | 3,594 cases / 1,555,177 assertions passed; 72 marked optional skips |
| Full Linux client, ASan/UBSan | 3,600 cases / 1,555,626 assertions passed; 72 marked optional skips; no sanitizer findings |
| Windows affected-area regressions | 52 cases / 82,037 assertions passed |
| Linux affected-area regressions, ASan/UBSan | 52 cases / 82,037 assertions passed |
| Windows bounded real-asset checks | 136 cases / 3,682 assertions passed |
| Linux bounded real-asset checks, ASan/UBSan | 181 cases / 37,181 assertions passed |
| Final production-shape TAA integration | All 11 shapes, 14,676 vertices and 8/8 submitted/presented frames passed readback |

The native smoke loads the actual new DLL with precisely the manifest's capability mask 2047.
It attaches the real native extension through `ExtensionChain` to `GameConnection`, then renders
the production Modern map/minimap and Gamepad map/Find scripts. Synthetic nontracker, trained
tracker and GM cases verify that stock NPC Find stays empty and closed while only permitted
Bastion markers remain. Activation is also verified in WorldAuth with no player or spawns, from
only the real world announcement before the InGame/Zoned callbacks. Saved stock NPC marks are enabled before ownership begins, with stale
selection, hover, menu and cached rows. Ownership retires those immediately; another peer restores
fresh stock data; old observation tokens remain rejected after restoration. Opaque policy revision
and context values follow the documented lifetimes. Geometry, static labels, group/raid markers and saved personal pins/preferences
remain intact. Passive rendering sends no captured command or raw addon packet.

Native regressions also cover exclusive marker providers, deny-wins composition, old SDK hosts,
zoning, exact tracking boundaries, actor/player/connection replacement and Find-owned walk
retirement without cancelling manual navigation. Delayed server path replies have no wire nonce;
ambiguous retired batches are discarded, and an unresolved expired batch keeps server-reply
admission quarantined until zoning. Ordinary local navigation remains available.

These checks use frozen client source tree `f012a3e60682a1a2c553e7f18571002c60524fe2`; the
paired executable identifies its actual build as `c5def65b`. Its executable/addon measurements,
review-only approval SQL and archives are preserved. They do not approve the new canonical pair.
Synthetic rendering and automated tests do not establish live server acceptance.
The fresh TAA smoke exercises the world-geometry renderer and presentation; it does not exercise
the zone renderer's texture-descriptor bindings or repeat the earlier terrain performance benchmarks.

## Earlier addon and integration baseline

The remaining results describe the preceding combined preview and hub/telegraph work. That baseline was
`4fd9804c` on `5ffa7ac8`; its source tree is identical to `a4b25781`.

| Check | Result |
|---|---|
| MSVC addon, companion public SDK, Modern/Gamepad helpers | 29/29 CTest checks passed |
| MSVC standalone build, bundled SDK only | 22/22 checks passed; full host-pack tests are explicitly unavailable without the client helpers |
| Linux address/undefined-behavior sanitizers | 18/18 C++ checks passed, no sanitizer findings or compiler warnings |
| Modern and Gamepad Lua | All 11 suites passed, including actual native pack helpers for seven tools, isolated follow/raid/stock-action widgets and the canonical host adapter |
| Stock Raid action adapter | 566 checks passed across both packs, including fresh source-bound wrappers, handle retirement/repair, selection-free List and overlay-off operation |
| Native raid manager backend | 215 focused MSVC checks passed; separate stock/settings review tokens, current owner/member/revision and live chat permission covered |
| Actual DLL, stock Modern/Gamepad Raid presenters and native callback bridge | 262 offline checks passed using the exact manifest capability mask 2047, with overlay/manage off |
| Final Windows focus with real Bastion assets | 220 cases / 38,059 assertions passed, including hooks/extensions, resized stock bags and Gamepad Group-role/HUD mouse regressions |
| Final Linux ASan/UBSan focus with real Bastion assets | 220 cases / 38,075 assertions passed; no source overlays or sanitizer findings |
| Full generic Windows combined client | 3,551 cases / 1,554,653 assertions passed; 72 marked skips with assets/vectors unset |
| Full generic Linux combined client, ASan/UBSan | 3,557 cases / 1,555,102 assertions passed; 72 marked skips with assets/vectors unset; no sanitizer findings |
| Native integrity protocol | Actual addon reports pass the independent server decoder/evaluator; nonce, approval, retry, malformed input and acknowledgement cases covered |
| Prepared world-server integration | Patch passes `git apply --check`; modified translation unit passes MSVC syntax compilation against the actual server headers; existing-source warnings remain |

The server source was read without modification. The server patch and binary-pair SQL are prepared
for review; no database was contacted and no server binary was deployed. The server change has not
been linked into a production world executable or tested with an ordinary live account.

The actual DLL probe loads Bastion through `NativePlugins`, runs its Lua in the real `LuaUi`, and
dispatches through the stock presenters, extension registry and native UI-function bridge. It
checks exact List/Add/Remove commands, members without a current actor, current selection, roster
revision and owner-incarnation invalidation, fresh reviews, and individual READ/UI/CHAT revocation.
Painting and metadata queries send no action. Each submitted request runs as `bastion` with exactly
the manifest's permissions; no network/web grant is added. Restoring revoked UI permission uses
the existing explicit plugin-reload lifecycle. Server authority and replies remain live acceptance.

The final shared API includes method hooks and owned display/action extensions. The addon uses
the extensions for the stock Raid window; server commands, roster checks and review tokens remain
in the addon. The real Modern/Gamepad presenter navigation fixture also passed 368 assertions.

## Compact hub presentation

The later hub polish changes Lua presentation only. Tools uses nine compact launchers grouped by
Character, Equipment and Raids; Map and Merchant options share Settings. Settings uses short
checkbox labels, concise notes and expandable binary details. The Modern default is 900 by 520
for Tools/Settings, with additional room for Firefell details and the unchanged command browser.

The existing hub regression passed 5,682 checks with both pack helpers. Nine native Vulkan
captures cover Tools and Settings in Modern/Gamepad at 1000 by 760 and 720 by 700, plus the
expanded Firefell route. All controls fit without clipping, nested card scrollbars, overlap or UI
errors; drawing emitted no commands or packets. These are synthetic UI checks. The compiled
addon/client and their approved binary-pair hashes are unchanged by this layout update.

## Telegraph appearance and ground placement

Tests join the actual production addon geometry to the native `WorldGeometryHost`, scene surface
enumeration and patch validator, then submit those vertices through the Vulkan world renderer.
They cover all eleven shapes, a sloped stair fixture, low boxes, four off-grid/rotated thin props at
two heights, a donut over a real floor hole, and changing lift surfaces. The thin-prop checks include
72 top samples; the terrain fixture includes 25. Derived seams stay within four XY float ULPs
(largest observed gap approximately `0.00000648` EQ units in these fixtures).

Three-run median CPU geometry times, with trace I/O and file export excluded:

| Native fixture | Time |
|---|---:|
| All eleven flat shapes together | 0.803 ms |
| Complex sloped/stair fixture | 12.161 ms |
| Radius-100 marker covering four small props | 2.240 ms |
| Same prop scene with the taller low box | 2.288 ms |
| Donut over an origin floor hole | 1.666 ms |

These are fixture timings, not complete game-frame or live-raid performance. The complex stair
case is a remaining performance limit. Flat and face-area reuse requires native whole-area surface
proofs; it is scoped to one marker build and never survives into a later frame. Unknown surfaces,
incomplete proofs and exhausted budgets defer an entire marker rather than certify guessed heights.

GPU readback checks pass on the RX 9070 XT for MSAA 1x/4x, TAA, TAA with actual frame generation,
and FSR3. Final production-shape captures use real submitted and presented TAA frames. The Vulkan
validation layer is unavailable on this machine, so no validation-layer result is claimed.
The captures use synthetic test scenery and are not screenshots of a live encounter.
The final combined UI/services build also passed a fresh production-geometry TAA readback with
eight submitted and presented frames. The timings above remain the original geometry-fixture
measurements; the UI integration does not change that geometry implementation.

Placement follows renderer-selected terrain, props and current scenery animation. Texture-alpha
cutouts retain their mesh footprint; shader-only deformation and creature bodies are not queried
as ground surfaces. Movement collision remains unchanged.

## Host and live coverage

Focused Windows and Linux checks cover the generic services, real-asset visual-floor queries,
surface proof kernel, guide visibility, item/loot flow and measured executable/loaded-copy identity.
Full generic client runs do not exercise asset/vector-dependent cases when those inputs are unset.
Recorded packet vectors were unavailable.

An earlier asset-wide run exposed four portable-fixture issues that were corrected: optional
server-specific files, a Windows symlink privilege assumption, and an empty map-label assertion
that could pass without finding a label. Two unchanged dataset assumptions remain outside this
change: an expected global model count above 150 (this set supplies 98), and an unavailable mount
model. Focused affected-asset and GPU checks are reported separately from that asset-wide run.

Live server admission, encounter timing and visibility, large raids and roster churn, slopes/lifts
in gameplay, item/merchant acknowledgements, pet equipment, home-city grants, Firefell transfers
and physical controller input remain acceptance work. Remote raid mana/endurance require server
observations. Deployed delegated `#raidmove`/`#raidpromote` handlers still need confirmation.
