# Bastion for Hailstone

A standalone Bastion addon with native Hailstone Lua windows for Modern and Gamepad.
Open **`/bastion`** for tools, character settings, and the searchable server command catalog.
`/citadel` and `/cit` remain compatible aliases.

Preview 4 targets upstream Hailstone `6446875a`, including its public server add-on services,
modular UI extensions and map ownership. The map-local filter panel uses `hs.UI` 1.5.
Hailstone builds missing those services can load the addon with its Bastion features inactive.

## Included

| Feature | Behavior |
|---|---|
| Enhanced Citadel follow | Manual start/stop, outside-group player following, configurable distance, movement cancellation, smooth steering/pitch, optional instant mount response. Direct follow does not route around obstacles. |
| Right-click merchant selling | **Off by default.** A deliberate inventory right-click while shopping sells the whole stack using Bastion's protocol. Occupied cursor, bags, unrelated slots and invalid merchant contexts retain the host's guarded behavior. |
| Maps | **Map controls → Bastion filters** selects group members, raid names, NPCs, merchants, other players and corpses. Tracking permissions and Citadel's reduced planar NPC/corpse range still apply. Stock NPC overlays and NPC Find stay disabled. Self, terrain, labels, pins and other-server stock preferences are preserved. Ordinary unrelated players remain unnamed. |
| Raid bars | Native Lua group cards or class grid, HP/class filters, class colors/roles, known resource bars, pets, targeting, explicit management, Delegate Raid Manager List/Grant/Revoke controls, edit/resize, scrolling and per-character settings. |
| Faction standings | FAC1 categories, global search, numeric values/progress, retained selection, inspection and TSV export, including `/outputfile faction`. |
| DZ manager | Saved expeditions, eligibility/expiry, explicit rejoin, and preserved delegate/server command paths. Rejoin does not teleport or renew an instance. |
| Pet inventory and gear | Eighteen slots, native cursor equip/drop/removal, inspection, ownership/cooldown checks and controller-friendly reviewed actions. Server replies own the inventory. |
| Kinbound | Item labels, explanation, nested-item loot warnings and safe mixed Loot All confirmations. Sharing and attunement remain server decisions. |
| Gear ratings | Server-provided item/augment ratings, equipped and best-owned summaries, configuration-aware metadata and retry state. |
| Home city | Authoritative selection grants and choices, explicit review, refresh and reminder handling. |
| Encounter telegraphs | Ground-conforming shapes, coral/blue/gold/mint roles, fine white glyphs, clean boundaries, compact impact clocks, restrained urgency pulses and a high-contrast preset. Surface checks follow the rendered terrain and props. Required warnings retain their visibility floor. |
| Firefell | FFZ1 current-instance negotiation and naming for zone 406, versions 249–255. Old context cannot label a later instance. |
| Server shortcuts | DLL-compatible slash shortcuts and a 205-entry registered server/quest command catalog, with staff access labels, search, help, copy, and explicit argument preparation/send. |
| Native integration checks | The existing DLL check receives the actual loaded `bastion.dll` checksum before login file checks. Newer servers can challenge the actual Hailstone executable/addon pair. Settings distinguish an unconfirmed legacy submission from explicit native acceptance. Existing game-file checks continue. |
| Stock Raid window | Delegate Raid Manager List/Grant/Revoke buttons beside the selected member's actions, with reviewed requests and server replies in chat. These controls work independently of the raid-bar overlay. |

Sentinel, nameplates and extended spell IDs are supplied by their existing addons or Hailstone itself.
No Item Search is bundled.

See the [parity review](docs/PARITY.md) for gaps found, the [validation report](docs/VALIDATION.md),
and the [integrity integration guide](docs/INTEGRITY.md) for DLL allowlist approval and the optional newer server protocol.
The [performance review](docs/PERFORMANCE.md) records the map-stall fix, hidden-window work,
measured costs and remaining live acceptance limits.

## Useful commands

```
/bastion
/bastion settings
/bastion commands
/raidbars
/follow citadel on
/follow distance 15
/follow on
/follow off
/rightclicksell on
/faction [partial name]
/outputfile faction [filename]
/dzmanager
/dzmanager add CharacterName
/petinventory
/setstartcity
/gearratings
/firefell
/telegraphs
/telegraphs test all
/season status
/requestdz CharacterName
```

The command browser includes every command parsed from the inspected server's native and quest
registries, plus the addon's local commands. Runtime database aliases, access overrides and later
server changes can differ; `#help` remains the server's current authority. Preparing a command does
not execute it. The server determines whether a submitted command is permitted.

Settings are kept in Hailstone's own per-server/per-character plugin directories. Faction exports
go into the addon's `exports` directory. Nothing writes to the player's game asset folder.
Start the addon before world login to receive the server announcement and any early protocol messages.
If it first loads at zone-in or is reloaded in a zone, it can also activate for Bastion - Production
through the official Bastion login server.

The stock Raid controls use `hs.UI.extensions` in both UI packs. They list managers or prepare a
grant/revoke request for the selected character, including members in another zone. Opening or drawing
the Raid window sends no command. The server checks actual raid leadership and reports the result
in chat; client leadership flags are not treated as proof of permission.

## Build and validate

```
cmake -S . -B build -DHAILSTONE_SDK_DIR=/path/to/hailstone/sdk
cmake --build build
ctest --test-dir build --output-on-failure
```

Lua 5.4 enables the two-pack UI regression tests. With GCC/Clang, add `-DBASTION_SANITIZE=ON`
for address and undefined-behavior checks, or run `tools/test-sanitizers.sh /path/to/sdk`.
The checked-in SDK is an MIT-licensed snapshot for standalone builds. The DLL uses the current public ABI 3
entry points defined by `HAILSTONE_PLUGIN`. Optional host services are checked before use.
See [SDK provenance and compatibility](sdk/README.md).
The full UI suite also reads Hailstone's native pack helpers. Providing the companion SDK above
detects them automatically; with the bundled SDK, set `-DHAILSTONE_CLIENT_UI_ROOT=/path/to/hailstone`
to enable those integration tests. SDK-only builds retain the C++ and isolated Lua tests.

Package with Hailstone's `tools/make_addon.py`. `tools/package.py` stages the plugin,
Lua windows, license and documentation. Build output and signing keys do not belong in source control.

## Acceptance limits

Live acceptance requires an ordinary-account login. Servers enforcing the existing DLL check must
allowlist the exact `bastion.dll` checksum alongside approved legacy DLLs. That protocol has no acknowledgement.
Servers enforcing CIT1 version 3 native integrity
must approve the exact executable and addon pair. Raidwide mana and endurance remain unavailable when
the server does not supply them. Legacy subgroup promotion uses the existing Bastion command, whose
deployed handler still needs confirmation.

Map filtering is client presentation policy. Other scripts can still read spawn data that the
server has sent; this is not a server-side anti-cheat boundary. This port intentionally closes
the legacy selected-target/nontracker corpse reveal exceptions to keep NPC/corpse map permissions
consistent across the map and minimap. Stock NPC Find is unavailable on Bastion; its list,
target/path/walk actions and quest discovery cannot bypass the addon's marker policy.
An active NPC Find walk stops on ownership or eligibility changes. Ordinary movement and
player/group navigation remain available. Other servers recover the saved stock map behavior.

Live encounters, slopes/lifts, 72-member raids, roster churn, pet equipment, merchant inventory
updates, home-city grants, Firefell transfers and physical controller use remain acceptance work.
No release or deployment is implied by a successful local build.
The validation report identifies the addon and client versions used for each test.
