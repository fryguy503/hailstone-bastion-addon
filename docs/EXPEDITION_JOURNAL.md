# Expedition Journal

Open **Bastion → Tools → Raids & adventures → Expedition Journal** or use
`/expeditionjournal`. The add-on uses Hailstone's embedded browser on Windows.
The guide is also available in a native window when the browser is unavailable.

The Bastion Library owns the public guide content. The journal first tries
`https://bastiongame.com/library/game/expedition-journal`, then uses its bundled
copy on the same Bastion origin if the dedicated view is unavailable. The old
Library remains available at `https://bastiongame.com/library/encounters`.

The browser and the client exchange fixed journal messages. The client queries
the signed-in player over the Bastion zone connection and delivers status only
to the add-on's journal page. The public Library export carries guide content.
Roster and Copium status require the matching Bastion quest deployment. The
server retains authority to admit or re-enter a raid, redeem relics, and apply
lore restrictions.

## Updating the bundled guide

Export the public `/library/api/v1/expeditions/mistmoore-eclipse` response and
capture the clean Laravel-rendered game journal view from a matching checkout.
The export and page can be generated from a development Library server during
release preparation. Run both generators before building the add-on:

```text
python tools/update_journal_catalog.py catalog.json
python tools/generate_expedition_journal_bundle.py <library-checkout> journal.html catalog.json
```

Build with the add-on's SDK and validate the hosted view, bundled browser view,
and guide-only fallback. The release package requests the browser permission
because the server's web policy applies to add-on tabs.
