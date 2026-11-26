# Third-party notices

The public Hailstone SDK headers are MIT licensed. Their complete notice is retained in
[sdk/LICENSE](sdk/LICENSE) and included in binary packages as `HAILSTONE-SDK-LICENSE.txt`.

The addon is built against that public interface. Hailstone supplies the Lua runtime, UI widgets,
asset loading and graphics services at runtime; they are not bundled in the addon package.

JSON for Modern C++ 3.11.2 is used to decode the authenticated tradeskill service responses.
Copyright © 2013–2022 Niels Lohmann. See the [MIT notice](docs/NLOHMANN-JSON-LICENSE.txt).

Server command names and player-visible help are a generated catalog of the inspected server
registries. `tools/generate_commands.py` records the extraction procedure. The server remains
the authority for command availability and permissions.

The Citadel version of MQ2Twist and its server's camp protocol were used as behavioral
references. The addon implements those behaviors independently through the public Hailstone
SDK; it does not include MacroQuest or Citadel source code. Item and AA twisting are disabled
in the inspected Citadel implementation and are not offered here.
