MWNet
======

MWNet is an independent, open-source multiplayer fork of [TES3MP](https://github.com/TES3MP/TES3MP) and [OpenMW](https://gitlab.com/OpenMW/openmw), the open-world RPG engine that supports playing Morrowind by Bethesda Softworks. You need to own Morrowind to play it.

Stock OpenMW 0.52 is a single-player engine. MWNet adds a dedicated server, a multiplayer client, encrypted direct connections, synchronized gameplay systems, and a server-side Lua API.

* MWNet version: 1.0.0-alpha.2
* OpenMW base version: 0.52.0
* Network protocol version: 13
* License: GPLv3 with additional allowed terms (see [LICENSE](LICENSE))
* Upstream engine: [OpenMW on GitLab](https://gitlab.com/OpenMW/openmw)
* Maintainer: Gabriel Max ([dev-zetta](https://github.com/dev-zetta))
* Source code and issue tracker: [dev-zetta/MWNet on GitHub](https://github.com/dev-zetta/MWNet)

Current status
--------------

MWNet 1.0.0-alpha.2 is a development alpha on OpenMW 0.52. Protocol 13 identifies the MWNet client/server pair and is intentionally incompatible with pre-rebrand clients, servers, and discovery signatures.

The underlying OpenMW engine supports completing the main quests in Morrowind, Tribunal, and Bloodmoon. Multiplayer adds more state and authority boundaries than single-player OpenMW, so server scripts, load order, content files, and MWNet versions must match between the server and every client. A local server is recommended when testing gameplay or diagnosing synchronization problems.

The major changes since the upstream 0.8.1 release include:

* the OpenMW 0.52 engine, rendering, input, Lua, navigation, and content-format improvements;
* a C++20 and Qt 6 migration of the MWNet client and dedicated server;
* an in-game saved/recent direct-connect screen with encrypted reachability probes and fingerprint display;
* an encrypted GameNetworkingSockets transport with trust-on-first-use server identities;
* a sol2-based server Lua binding layer and bundled 0.8.1 CoreScripts brought forward for 1.0.0;
* extensive startup, cell, actor, object, dialogue, death, and NPC AI crash fixes;
* corrected multiplayer melee and hand-to-hand damage propagation;
* restored MWNet chat shortcuts on OpenMW 0.52, including F2 and Y;
* real-time multiplayer simulation while inventory, dialogue, and other local GUI windows are open; and
* server-driven resurrection without the default single-player jail/menu interruption.

See the [MWNet changelog](mwnet-changelog.md) for the detailed release history and OpenMW's [engine changelog](CHANGELOG.md) for upstream changes.

The standalone browser and legacy master service have been removed. The new HTTPS directory API v1 and in-game public browser are part of the 1.0.0 release scope. Announcing is opt-in and the directory URL is unset until an operator configures a hostname they control; direct connect remains available during directory outages. See [public discovery deployment and API](docs/public-discovery.md).

Multiplayer features
--------------------

MWNet provides synchronization and server authority for:

* player movement, combat, spells, animations, equipment, stats, death, and respawning;
* NPC and creature position, AI, combat, equipment, spells, stats, and death;
* cells, weather, time, containers, doors, traps, object state, and world map exploration;
* dialogue, journals, factions, reputation, bounties, kill counts, and other quest state;
* custom records and selected client-side script variables; and
* persistent player and world state implemented by server-side Lua scripts.

Servers can customize or replace much of the multiplayer behavior through the API and bundled [CoreScripts](files/mwnet/core-scripts). MWNet does not turn an unmodified OpenMW executable into a multiplayer client, and OpenMW save games are not a substitute for server-managed state.

Compatibility and known limitations
-----------------------------------

All clients and the server must use the same MWNet release, network protocol, content files, and load order. A hit sound or attack animation alone does not prove that authoritative damage was accepted by the server.

Mods created for the original Morrowind engine can be hit-or-miss in OpenMW because its script compiler is stricter and some mods depend on original-engine quirks. Multiplayer also requires synchronization support for stateful scripts and mechanics. Client-side script variables must be explicitly synchronized when a server needs them; synchronizing every variable would create excessive packet traffic.

Getting started
---------------

* [Quick start](QUICKSTART.md)
* [Build instructions](BUILD_INSTRUCTIONS.md)
* [OpenMW installation documentation](https://openmw.readthedocs.io/en/latest/manuals/installation/index.html)
* [MWNet credits](mwnet-credits.md)
* [Security policy](SECURITY.md) and [threat model](THREAT_MODEL.md)
* [1.0 release gates](RELEASE_GATES.md)
* [Build and validation instructions](BUILD_INSTRUCTIONS.md)
* [Lua 0.8.1 compatibility](LUA_API_COMPATIBILITY.md)
* [Code of Conduct](CODE_OF_CONDUCT.md)
* [Migrating to MWNet](docs/mwnet-migration.md)

Local server testing
--------------------

After building the client and server, start an isolated localhost test with one command:

```bash
./run-mwnet-local.sh
```

The launcher synchronizes CoreScripts, creates persistent client and server test state under `.mwnet-test`, starts the server on `127.0.0.1:25565`, launches the client without the single-player intro and chargen sequence, and stops the server when the game closes. A new test client profile is seeded from the current OpenMW profile so its display, input, camera, and Lua settings are preserved. On the first run the launcher also detects the Morrowind data directory from OpenMW's configuration or asks for it and remembers the answer. Either source can be supplied explicitly:

```bash
./run-mwnet-local.sh \
    --data-dir "/path/to/Morrowind/Data Files" \
    --client-profile "/path/to/existing/openmw/profile" \
    --server-profile "/path/to/existing/corescripts/server"
```

To run only the isolated server, use `./run-mwnet-server.sh`. Run either script with `--help` for configuration overrides. Start with identical vanilla content and load order on both sides before adding mods.

Alpha servers refuse non-loopback listen addresses by default. An operator who deliberately exposes a server must set both `localAddress` to the desired numeric address and `publicListen = true` in the `[General]` section of `mwnet-server.cfg`; firewall and network exposure remain the operator's responsibility.

The data path
-------------

The data path tells MWNet/OpenMW where to find the Morrowind files. The launcher can usually detect a properly installed copy, including one installed under Wine. Use the same content list and order for the client and local server test.

Default multiplayer controls
----------------------------

* `Y`: focus chat and begin writing a message.
* `F2`: cycle or hide the chat window.

These keys are configured in the `[Chat]` section of `mwnet-client-default.cfg`. During gameplay, a configured MWNet chat shortcut takes priority over an OpenMW action bound to the same key; rebind the OpenMW action if both functions are needed.

Contributing
------------

Submit bug reports to the [MWNet issue tracker](https://github.com/dev-zetta/MWNet/issues). Include the MWNet version, operating system, server scripts, content list and order, whether the problem reproduces on a local server, and whether it reproduces in stock OpenMW 0.52 single-player.

Keep changes focused and discuss large multiplayer or protocol changes before implementation. Include the relevant build checks and, for synchronization changes, results from a matching local client/server test.

Protocol and server-only changes can be checked without the OpenMW client dependencies by configuring with `MWNET_TESTS_ONLY=ON`. The complete fuzz, encrypted integration, persistence fault-injection, soak and performance commands are documented in the [build instructions](BUILD_INSTRUCTIONS.md) and enforced by the [1.0 release gates](RELEASE_GATES.md).

By participating in the project, contributors agree to follow the [Code of Conduct](CODE_OF_CONDUCT.md). Report security issues through the private process in [SECURITY.md](SECURITY.md), not a public issue.

Credits and licenses
--------------------

MWNet is a fork of TES3MP, founded by David Cernat and Stanislav Zhukov, and OpenMW. Gabriel Max maintains this independent fork. Original copyright and license notices are preserved as upstream attribution. See [mwnet-credits.md](mwnet-credits.md) and [AUTHORS.md](AUTHORS.md) for the full contributor lists.

Font licenses:

* DejaVuLGCSansMono.ttf: custom; see [DejaVuFontLicense.txt](files/data/fonts/DejaVuFontLicense.txt).
* DemonicLetters.ttf: SIL Open Font License; see [DemonicLettersFontLicense.txt](files/data/fonts/DemonicLettersFontLicense.txt).
* MysticCards.ttf: SIL Open Font License; see [MysticCardsFontLicense.txt](files/data/fonts/MysticCardsFontLicense.txt).
