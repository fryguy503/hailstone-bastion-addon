# Third-party notices

The public Hailstone SDK headers are MIT licensed. Their complete notice is retained in
[sdk/LICENSE](sdk/LICENSE) and included in binary packages as `HAILSTONE-SDK-LICENSE.txt`.

The addon is built against that public interface. Hailstone supplies the Lua runtime, UI widgets,
asset loading and graphics services at runtime; they are not bundled in the addon package.

Server command names and player-visible help are a generated catalog of the inspected server
registries. `tools/generate_commands.py` records the extraction procedure. The server remains
the authority for command availability and permissions.
