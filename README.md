TES3MP
======

TES3MP is an open-source multiplayer fork of [OpenMW](https://gitlab.com/OpenMW/openmw), the open-world RPG engine that supports playing Morrowind by Bethesda Softworks. You need to own Morrowind to play it.

Stock OpenMW 0.52 is a single-player engine. TES3MP adds a dedicated server, a multiplayer client, an in-game server browser, synchronized gameplay systems, and a server-side Lua API.

* TES3MP version: 1.0.0
* OpenMW base version: 0.52.0
* Network protocol version: 10
* License: GPLv3 with additional allowed terms (see [LICENSE](LICENSE))
* Upstream engine: [OpenMW on GitLab](https://gitlab.com/OpenMW/openmw)
* Maintainer: Gabriel Max ([dev-zetta](https://github.com/dev-zetta))

Current status
--------------

TES3MP 1.0.0 is the first major release of this maintained fork. It updates the engine base from OpenMW 0.47 to 0.52 while preserving TES3MP's multiplayer architecture and server scripting API.

The underlying OpenMW engine supports completing the main quests in Morrowind, Tribunal, and Bloodmoon. Multiplayer adds more state and authority boundaries than single-player OpenMW, so server scripts, load order, content files, and TES3MP versions must match between the server and every client. A local server is recommended when testing gameplay or diagnosing synchronization problems.

The major changes since TES3MP 0.8.1 include:

* the OpenMW 0.52 engine, rendering, input, Lua, navigation, and content-format improvements;
* a C++20 and Qt 6 migration of the TES3MP client, server browser, and dedicated server;
* an in-game server-browser flow and a convenience client launcher;
* a vendored CrabNet networking dependency for reproducible builds;
* a sol2-based server Lua binding layer and bundled 0.8.1 CoreScripts brought forward for 1.0.0;
* extensive startup, cell, actor, object, dialogue, death, and NPC AI crash fixes;
* corrected multiplayer melee and hand-to-hand damage propagation;
* restored TES3MP chat shortcuts on OpenMW 0.52, including F2 and Y;
* real-time multiplayer simulation while inventory, dialogue, and other local GUI windows are open; and
* server-driven resurrection without the default single-player jail/menu interruption.

See the [TES3MP changelog](tes3mp-changelog.md) for the detailed release history and OpenMW's [engine changelog](CHANGELOG.md) for upstream changes.

Multiplayer features
--------------------

TES3MP provides synchronization and server authority for:

* player movement, combat, spells, animations, equipment, stats, death, and respawning;
* NPC and creature position, AI, combat, equipment, spells, stats, and death;
* cells, weather, time, containers, doors, traps, object state, and world map exploration;
* dialogue, journals, factions, reputation, bounties, kill counts, and other quest state;
* custom records and selected client-side script variables; and
* persistent player and world state implemented by server-side Lua scripts.

Servers can customize or replace much of the multiplayer behavior through the API and bundled [CoreScripts](files/tes3mp/core-scripts). TES3MP does not turn an unmodified OpenMW executable into a multiplayer client, and OpenMW save games are not a substitute for server-managed state.

Compatibility and known limitations
-----------------------------------

All clients and the server must use the same TES3MP release, network protocol, content files, and load order. A hit sound or attack animation alone does not prove that authoritative damage was accepted by the server.

Mods created for the original Morrowind engine can be hit-or-miss in OpenMW because its script compiler is stricter and some mods depend on original-engine quirks. Multiplayer also requires synchronization support for stateful scripts and mechanics. Client-side script variables must be explicitly synchronized when a server needs them; synchronizing every variable would create excessive packet traffic.

Getting started
---------------

* [Quick start](QUICKSTART.md)
* [Build instructions](BUILD_INSTRUCTIONS.md)
* [OpenMW installation documentation](https://openmw.readthedocs.io/en/latest/manuals/installation/index.html)
* [TES3MP credits](tes3mp-credits.md)
* [Legacy TES3MP wiki](https://github.com/TES3MP/TES3MP/wiki)
* [TES3MP community Discord](https://discord.gg/ECJk293)
* [TES3MP section on the OpenMW forums](https://forum.openmw.org/viewforum.php?f=45)

Local server testing
--------------------

After building both targets, copy the bundled CoreScripts into the server's working directory:

```bash
mkdir -p build/server
cp -a files/tes3mp/core-scripts/. build/server/
```

Before starting an isolated LAN-only test, create `tes3mp-server.cfg` in the user's OpenMW configuration directory, set `localAddress = 127.0.0.1`, and set `enabled = false` under `[MasterServer]`. Then start the server:

```bash
cd build
LD_LIBRARY_PATH="$PWD/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ./tes3mp-server
```

Connect the client to `127.0.0.1` on the default port `25565`. Start with identical vanilla content and load order on both sides before adding mods.

The data path
-------------

The data path tells TES3MP/OpenMW where to find the Morrowind files. The launcher can usually detect a properly installed copy, including one installed under Wine. Use the same content list and order for the client and local server test.

Default multiplayer controls
----------------------------

* `Y`: focus chat and begin writing a message.
* `F2`: cycle or hide the chat window.

These keys are configured in the `[Chat]` section of `tes3mp-client-default.cfg`. During gameplay, a configured TES3MP chat shortcut takes priority over an OpenMW action bound to the same key; rebind the OpenMW action if both functions are needed.

Contributing
------------

Bug reports should identify the TES3MP version, operating system, server scripts, content list and order, whether the problem reproduces on a local server, and whether it reproduces in stock OpenMW 0.52 single-player.

Keep changes focused and discuss large multiplayer or protocol changes before implementation. Include the relevant build checks and, for synchronization changes, results from a matching local client/server test.

Credits and licenses
--------------------

TES3MP was founded and developed by David Cernat and Stanislav Zhukov on top of OpenMW. See [tes3mp-credits.md](tes3mp-credits.md) and [AUTHORS.md](AUTHORS.md) for the full contributor lists.

Font licenses:

* DejaVuLGCSansMono.ttf: custom; see [DejaVuFontLicense.txt](files/data/fonts/DejaVuFontLicense.txt).
* DemonicLetters.ttf: SIL Open Font License; see [DemonicLettersFontLicense.txt](files/data/fonts/DemonicLettersFontLicense.txt).
* MysticCards.ttf: SIL Open Font License; see [MysticCardsFontLicense.txt](files/data/fonts/MysticCardsFontLicense.txt).
