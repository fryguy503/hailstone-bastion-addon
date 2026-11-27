# Changes

## 0.1.0-preview.21

- Show the first discoverer in Modern and Gamepad item details when Bastion supplies discovery information.
- Show an item's base coin value in platinum, gold, silver and copper with Hailstone Bastion Preview 05 or a client that provides the item value field.

## 0.1.0-preview.20

- Combine ToFS Group and Raid into one Tower of Frozen Shadow page with a mode switch.
- Give each expedition an illustrated native banner and matching status and entry cards.
- Open native item previews for named journal rewards and entry items, and spell details for published spell references supported by the client.

## 0.1.0-preview.19

- Gather all expedition pages in one selector in Modern and Gamepad. The Encounter Library and Copium tracker keep direct shortcuts.

## 0.1.0-preview.18

- Track live entry, replay, saved runs and roster eligibility for ToFS Group, ToFS Raid, the Broken Accord, and Firefell.
- Show the ToFS Raid Writ and offering separately. Show chapter access and the separate completion lockout for Firefell.

## 0.1.0-preview.17

- Keep Mistmoore encounter columns equal and allow room for wrapped guide text.
- Add journal shortcuts for the Tower of Frozen Shadow group and raid expeditions, the Broken Accord, Firefell, and Sleeper's Tomb.
- Show the public Tomb phase, next Static Mode phase, and saved run phase when Bastion supplies them.

## 0.1.0-preview.16

- Click an encounter in the Mistmoore guide to read its overview, mechanics, and role tips in the journal.
- Browse the bundled Bastion Encounter Library. Search or filter by zone and encounter type.

## 0.1.0-preview.15

- Make the Expedition Journal native in both Modern and Gamepad. The eclipse layout, encounter guides, roster, and Copium tracker open directly in the client UI.
- Read live journal status through the existing Bastion game connection. The bundled catalog is available offline.
- Remove the Web permission from the add-on.

## 0.1.0-preview.14

- Add **Bastion → Tools → Equipment → Tradeskill Plans** and the `/tradeskillplans` shortcut.
- Browse Bastion recipes by tradeskill or name. Preview a target quantity and save the plan to your linked Bastion account.
- Check your bags, edit recipe choices and manual inventory, and pin your next steps while crafting. The website and game share checklist progress.
- Import older browser saves from the website's **Saved Tradeskill Plans** page in the browser that holds them. The browser keeps its copy.
- Keep pending edits locally until Bastion confirms the save. Changes made on another device prompt you to review the current plan.

## 0.1.0-preview.13

- Add Expedition Journal to **Bastion → Tools → Raids & adventures**, with the `/expeditionjournal` shortcut.
- Show Mistmoore Eclipse and Midnight Vigil entry instructions, requirements and encounter guides. Live Bastion quest support adds personal unlocks, replay timers, re-entry, and leader roster checks.
- Add Master Artificer Copium's tracker for exact individual encounter credit, owned relics and reward choices.
- The journal uses the embedded browser and requests the `web` permission. The bundled native guide remains available without it.

## 0.1.0-preview.12

- Add Raid Ready Check to Modern and Gamepad. Raid leaders can start a 45-second check or end it early, and members can change their answers while it is open.
- Show each player's response beside HP and report totals by subgroup. The response window works with Raid Bars hidden or set to clickthrough.
- This Windows preview requires Hailstone 0.7.3-beta or newer. Shared checks require the matching Bastion server update.

## 0.1.0-preview.10

- Allow server-managed dire-charmed pets to equip cursor items and return eligible gear through Pet Inventory.
- Refresh the window when it opens and resync equipment after ordinary pet hand-ins.

## 0.1.0-preview.9

- Keep active encounter warnings and server updates across skipped background draws.
- Reuse terrain work for actor-following warnings while their resolved placement and terrain are current.
- Show an encounter alert in Modern and Gamepad when a ground warning cannot draw.
- Retry unacknowledged telegraph readiness renewals before the lease can expire.

## 0.1.0-preview.8

- Keep verified encounter warning footprints visible while other markers use the terrain budget.
- Separate impact clocks from the warning footprint and make the clock strokes easier to see.
- Resume interrupted terrain work on stationary markers. Invalidate cached geometry when its marker or supporting surfaces change.
- Add telegraph status counts for reused and deferred required warnings. Use bounded terrain work and regional cache validation when supported by Hailstone.

## 0.1.0-preview.7

- Restore Find and server paths for NPCs that Bastion marks findable on compatible Hailstone clients.
- Enable visual guides to your current target and a selected Bazaar trader when supported by Hailstone.
- Restore Citadel's corpse shortcuts and add a corpse visibility selector in both packs using Hailstone's saved mode.

## 0.1.0-preview.6

- Add Citadel song twisting: gem queues, once/hold/resume controls, historical timing, quiet settings and the `Twist` data object.
- Yield to manual casts and native melody; stop across character, zone and permission changes without starting automatically.
- Add `/fastcamp` and optional server-offered fast camp using the native camp destination and normal-countdown fallback.
- Add compact twist and camping controls to both Settings pages and include all new commands in the command browser.
- Use the companion client's token-bound popup services and raw duration fields, with explicit fallback on older hosts.

## 0.1.0-preview.5

- Keep the original login verification state across zone transfers instead of submitting a fresh report that the server intentionally ignores while zoning.
- Show **Reconnect required** when Bastion first loads after the login checks, and cancel incomplete reports when the login's WORLD connection ends.
- Preserve the real server acknowledgement and DLL measurement from a completed login without per-zone rehashing or retransmission.

## 0.1.0-preview.4

- Submit the measured, loaded `bastion.dll` checksum through Bastion's existing DLL integrity check before login file checks or EnterWorld.
- Measure once per login, retry only the cached report up to three times, and prefer the newer challenge protocol whenever offered.
- Distinguish legacy submission from server acceptance in both UI packs, and provide a review-only tool to append the checksum to the server's DLL allowlist.

## 0.1.0-preview.3

- Build with upstream Hailstone `6446875a` and use its standard native plugin entry points.
- Read raid and map roster data through the public bulk raid API and use upstream's raid targeting and move calls.

## 0.1.0-preview.2

- Removed repeated raid-member enumeration from map filtering while retaining fresh roster and actor checks.
- Avoided full state generation for hidden windows and disabled raid bars, and removed repeated settings-directory filesystem work.
- Retired stale panel state on zoning/logout and kept character settings isolated across reconnects and failed path resolution.
- Fixed the raid overlay on hosts missing the optional scrollbar flag; shortened and ordered manager actions.
- Wrapped action and review buttons cleanly in both UI packs, including narrow windows and differing UI scales.
- Stacked narrow browser panes and made home-city details and choices readable at smaller widths.
- Added native performance measurements and regression coverage; the reported live Inventory timeout remains under investigation.

## 0.1.0-preview.1

- Rebuilt against Canonical 2 of the public SDK after the released raid-bulk field was added.
  Current and prior marked layouts use separate typed host views.

- Added compact map-local filters shared with Bastion settings, including an independent group-member toggle.
  Tracking permissions still apply, self remains visible, and explicit filters also work for GMs.

- Migrated to the canonical ABI 3 SDK layout, preserving four released bulk-reader slots before the addon services.
  The DLL uses layout negotiation and a versioned host setter; older clients refuse it before initialization.

- Claimed Bastion map ownership, disabling stock NPC overlays and NPC Find while preserving terrain,
  labels, pins and stock preferences on other servers. Existing NPC Find walks stop when blocked.
- Tightened the hub's Tools and Settings layouts with compact groups, shorter labels and optional binary details.
- Integrated the modular `hs.UI` facade and owned stock-window extensions in both UI packs.
- Added a standalone Bastion plugin with Modern and Gamepad Lua windows.
- Added `/bastion` tools, settings and a searchable server command catalog.
- Added negotiated executable/addon integrity reporting, explicit server acknowledgement status
  and tools to prepare the corresponding server integration and binary-pair approval.
- Added follow, optional right-click selling, tracking map policy, native raid bars, factions,
  saved expeditions and delegates, pet gear, Kinbound, gear ratings, home-city selection,
  encounter telegraphs, Firefell context and server shortcuts.
- Refined telegraphs with a shared role palette, fine semantic glyphs, compact impact clocks
  and surface-boundary clipping over rendered terrain, small props and stair treads.
- Improved raid layouts and retained unknown-state and stale-identity checks across roster
  and equipment actions.
- Added raid-manager List, Grant and Revoke buttons to the stock Raid window and raid-bar settings,
  with current-roster selection, explicit grant/revoke review and server confirmation in chat.
- Kept Sentinel, nameplates, spell-ID expansion and Item Search outside this addon.
