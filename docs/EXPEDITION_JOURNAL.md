# Expedition Journal

Open **Bastion → Tools → Raids & adventures → Expedition Journal** or type
`/expeditionjournal`. Both Modern and Gamepad draw the journal in Hailstone's
native UI. Select an encounter in Normal or Bloodbound Mistmoore Eclipse or
Midnight Vigil to open its full guide. The Encounter Library provides
searchable published guides, with zone and encounter-type filters.

Use the **Expeditions** selector to switch between Mistmoore Eclipse, Midnight
Vigil, Tower of Frozen Shadow, the Broken Accord, Firefell, and Sleeper's Tomb.
Choose **Group** or **Raid** in the Tower of Frozen Shadow banner. The journal
remembers the last mode you selected there. The Encounter Library and Copium
tracker have their own shortcuts.
Sleeper's Tomb shows the public 1.0 or 2.0 phase separately from the next
Static Mode phase for your guild and the phase of an assigned active run.
ToFS, Broken Accord, and Firefell show personal entry checks, current replay,
saved run status, and a **Roster** tab. ToFS Raid also checks the shared Writ
unlock, held Splinters, and paid retry. Firefell shows its completion lockout
separately from the request timer and lists chapter access.

The add-on reads personal unlocks, replay timers, re-entry, leader roster
eligibility, and Master Artificer Copium credit and rewards over the authenticated
Bastion zone connection. The server keeps authority for admission and claims.
A game-server update with the journal quest handlers enables live status.
Guide content stays available whenever the server can't supply a snapshot.
Browsing guides does not request live character data.

Select a linked entry item or Copium reward to open Hailstone's item display.
An encounter guide can link a published ability to the native spell window
when the installed game files resolve its exact spell ID. The written tactics
remain available without a matching spell record.

The add-on clears snapshots on character and zone changes. A status check is
read-only, and a stale snapshot expires after 30 seconds. Use **Refresh** to
request current status before heading to the entry NPC.

## Updating the guide catalog

Use a Bastion site checkout with Composer dependencies installed to export the
published library. Run these commands in a shell that preserves UTF-8 output:

```text
php tools/export_native_encounter_library.php /path/to/bastion-site > public-encounters.json
python tools/generate_native_encounter_library.py public-encounters.json
python tools/generate_native_encounter_library.py public-encounters.json --check
```

Export the public `/library/api/v1/expeditions/mistmoore-eclipse` response from
the Bastion Library and regenerate both UI packs:

```text
python tools/update_journal_catalog.py catalog.json
```

Validate the in-game mode switch, guide-only state, saved-run re-entry and
replay together, roster visibility, and Copium reward selection. The Windows
package can be smoke-tested with Hailstone's browser component absent.
