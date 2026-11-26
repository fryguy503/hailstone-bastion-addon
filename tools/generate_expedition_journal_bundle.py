#!/usr/bin/env python3
"""Bundle the rendered Bastion Library journal and public catalog for the add-on."""
import argparse
from html import escape
from html.parser import HTMLParser
import json
from pathlib import Path
import re
from urllib.parse import urlsplit


PRIMARY_URL = "https://bastiongame.com/library/game/expedition-journal"
LIBRARY_URL = "https://bastiongame.com/library/encounters"
CATALOG_ID = "expedition-journal-catalog"
DELIMITER = "BJR1_JOURNAL"


class JournalMarkup(HTMLParser):
    """Ensure a response snapshot contains only the approved local journal body."""

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag in {"base", "embed", "form", "iframe", "link", "object", "script"}:
            raise ValueError(f"Unexpected element in journal body: {tag}")
        if any(name.startswith("on") or name == "src" for name, _ in attrs):
            raise ValueError(f"Unexpected active attribute on {tag}")
        if any(name == "href" and value != LIBRARY_URL for name, value in attrs):
            raise ValueError(f"Unexpected link on {tag}")


def journal_body(page: str) -> str:
    match = re.search(r"<body>(.*?)</body>", page, re.DOTALL)
    if match is None:
        raise ValueError("Expected the rendered Expedition Journal page")
    body = match.group(1)
    body, replacements = re.subn(
        rf'<script type="application/json" id="{CATALOG_ID}">.*?</script>',
        "", body, flags=re.DOTALL,
    )
    if replacements != 1 or 'id="bastion-expedition-journal"' not in body:
        raise ValueError("Expected the journal root and versioned catalog")
    body, replacements = re.subn(
        r'(<a class="ej-library" href=")[^"]+(" data-library)',
        rf"\g<1>{escape(LIBRARY_URL, quote=True)}\g<2>", body,
    )
    if replacements != 1:
        raise ValueError("Expected the Library navigation link")
    parser = JournalMarkup()
    parser.feed(body)
    parser.close()
    return body.strip()


def public_catalog(catalog: dict) -> str:
    if catalog["version"] != 1 or catalog["id"] != "mistmoore-eclipse":
        raise ValueError("Expected the Mistmoore Eclipse version 1 catalog")
    if catalog["libraryUrl"] != LIBRARY_URL or set(catalog["modes"]) != {"normal", "bloodbound", "vigil"}:
        raise ValueError("Unexpected Library origin or expedition modes")
    for mode in catalog["modes"].values():
        for guide in mode["guide"]:
            link = urlsplit(guide["url"])
            if (link.scheme, link.netloc, link.path, link.query, link.fragment) != (
                "https", "bastiongame.com", f"/library/encounters/{guide['slug']}", "", "",
            ):
                raise ValueError("Expected a first-party encounter guide")
    return json.dumps(catalog, separators=(",", ":"), ensure_ascii=False).replace("<", "\\u003c")


def render_script(library: Path, page: str, catalog: dict) -> str:
    assets = library / "public" / "game-journal"
    css = (assets / "expedition-journal.css").read_text(encoding="utf-8")
    script = (assets / "expedition-journal.js").read_text(encoding="utf-8").strip()
    signal = 'bridge.post("BJR1:ready");'
    if script.count(signal) != 1:
        raise ValueError("Expected one hosted journal handshake")
    script = script.replace(signal, 'window.bastionExpeditionJournalBundled = true;\n    bridge.post("BJR1:bundled-ready");')

    html = (
        '<!doctype html><html lang="en"><head><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width, initial-scale=1">'
        '<title>Expedition Journal</title></head><body>'
        f'{journal_body(page)}<script type="application/json" id="{CATALOG_ID}">'
        f"{public_catalog(catalog)}</script></body></html>"
    )
    # Execute this whole script through the native bridge. The restricted host's
    # CSP can keep rejecting inline scripts, and the adopted styles are local.
    return (
        "(() => {\n"
        '  "use strict";\n'
        f'  if (location.href !== "{PRIMARY_URL}" && location.href !== "{LIBRARY_URL}") return;\n'
        "  const bridge = window.hailstone;\n"
        '  if (typeof bridge?.post !== "function" || window.bastionExpeditionJournalBundled) return;\n'
        "  const stylesheet = new CSSStyleSheet();\n"
        f"  stylesheet.replaceSync({json.dumps(css, ensure_ascii=False)});\n"
        "  document.open();\n"
        f"  document.write({json.dumps(html, ensure_ascii=False)});\n"
        "  document.close();\n"
        "  if (window.hailstone !== bridge) window.hailstone = bridge;\n"
        "  document.adoptedStyleSheets = [stylesheet];\n"
        f"{script}\n"
        "})();\n"
    )


def header(script: str) -> str:
    if len(script.encode("utf-8")) >= 60_000 or f'){DELIMITER}"' in script:
        raise ValueError("Journal source is too large or contains the C++ raw string delimiter")
    # Chunks keep individual literals comfortably below the MSVC limit.
    chunks = [script[i:i + 4000] for i in range(0, len(script), 4000)]
    literals = "\n".join(f'R"{DELIMITER}({chunk}){DELIMITER}"' for chunk in chunks)
    return (
        "// SPDX-License-Identifier: MIT\n"
        "// Generated by tools/generate_expedition_journal_bundle.py from Bastion Library.\n"
        "#pragma once\n\n"
        "namespace bastion::journal {\n\n"
        f"inline constexpr char kExpeditionJournalFallbackScript[] =\n{literals};\n\n"
        "} // namespace bastion::journal\n"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path, help="Bastion Library checkout")
    parser.add_argument("page", type=Path, help="Rendered response from /library/game/expedition-journal")
    parser.add_argument("catalog", type=Path, help="JSON from /library/api/v1/expeditions/mistmoore-eclipse")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    catalog = json.loads(args.catalog.read_text(encoding="utf-8"))
    script = render_script(args.library, args.page.read_text(encoding="utf-8"), catalog)
    (root / "src" / "expedition_journal_bundle.inc").write_text(header(script), encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
