# Expedition Journal

Open **Bastion → Tools → Raids & adventures → Expedition Journal** or type
`/expeditionjournal`. Both Modern and Gamepad draw the journal in Hailstone's
native UI. The bundled catalog covers Normal and Bloodbound Mistmoore Eclipse
and Midnight Vigil.

The add-on reads personal unlocks, replay timers, re-entry, leader roster
eligibility, and Master Artificer Copium credit and rewards over the authenticated
Bastion zone connection. The server keeps authority for admission and claims.
A game-server update with the journal quest handlers enables live status.
Guide content stays available whenever the server can't supply a snapshot.

The add-on clears snapshots on character and zone changes. A status check is
read-only, and a stale snapshot expires after 30 seconds. Use **Refresh** to
request current status before heading to the Archivist.

## Updating the guide catalog

Export the public `/library/api/v1/expeditions/mistmoore-eclipse` response from
the Bastion Library and regenerate both UI packs:

```text
python tools/update_journal_catalog.py catalog.json
```

Validate the in-game mode switch, guide-only state, saved-run re-entry and
replay together, roster visibility, and Copium reward selection. The Windows
package can be smoke-tested with Hailstone's browser component absent.
