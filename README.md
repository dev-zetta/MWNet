TES3MP
======

TES3MP is an open-source multiplayer fork of [OpenMW](https://gitlab.com/OpenMW/openmw), the open-world RPG engine that supports playing Morrowind by Bethesda Softworks. You need to own Morrowind to play it.

Stock OpenMW 0.52 is a single-player engine. TES3MP adds a dedicated server, a multiplayer client, encrypted direct connections, synchronized gameplay systems, and a server-side Lua API.

* TES3MP version: 1.0.0-alpha.1
* OpenMW base version: 0.52.0
* Network protocol version: 11
* License: GPLv3 with additional allowed terms (see [LICENSE](LICENSE))
* Upstream engine: [OpenMW on GitLab](https://gitlab.com/OpenMW/openmw)
* Maintainer: Gabriel Max ([dev-zetta](https://github.com/dev-zetta))

Current status
--------------

TES3MP 1.0.0-alpha.1 is an unreleased hardening build on OpenMW 0.52. Protocol 11 is intentionally incompatible with legacy clients and servers while the multiplayer transport, parser, authority and persistence boundaries are rebuilt.

The underlying OpenMW engine supports completing the main quests in Morrowind, Tribunal, and Bloodmoon. Multiplayer adds more state and authority boundaries than single-player OpenMW, so server scripts, load order, content files, and TES3MP versions must match between the server and every client. A local server is recommended when testing gameplay or diagnosing synchronization problems.

The major changes since TES3MP 0.8.1 include:

* the OpenMW 0.52 engine, rendering, input, Lua, navigation, and content-format improvements;
* a C++20 and Qt 6 migration of the TES3MP client and dedicated server;
* an in-game saved/recent direct-connect screen with encrypted reachability probes and fingerprint display;
* an encrypted GameNetworkingSockets transport with trust-on-first-use server identities;
* a sol2-based server Lua binding layer and bundled 0.8.1 CoreScripts brought forward for 1.0.0;
* extensive startup, cell, actor, object, dialogue, death, and NPC AI crash fixes;
* corrected multiplayer melee and hand-to-hand damage propagation;
* restored TES3MP chat shortcuts on OpenMW 0.52, including F2 and Y;
* real-time multiplayer simulation while inventory, dialogue, and other local GUI windows are open; and
* server-driven resurrection without the default single-player jail/menu interruption.

See the [TES3MP changelog](tes3mp-changelog.md) for the detailed release history and OpenMW's [engine changelog](CHANGELOG.md) for upstream changes.

The standalone browser, legacy master service and automatic connection to `master.tes3mp.com` have been removed. Direct connect is the only discovery path supported for 1.0.0. Public discovery, if reintroduced later, will be a separately reviewed service rather than part of this release.

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
* [Security policy](SECURITY.md) and [threat model](THREAT_MODEL.md)
* [1.0 release gates](RELEASE_GATES.md)
* [Lua 0.8.1 compatibility](LUA_API_COMPATIBILITY.md)
* [Code of Conduct](CODE_OF_CONDUCT.md)
* [Legacy TES3MP wiki](https://github.com/TES3MP/TES3MP/wiki)
* [TES3MP community Discord](https://discord.gg/ECJk293)
* [TES3MP section on the OpenMW forums](https://forum.openmw.org/viewforum.php?f=45)

Local server testing
--------------------

After building the client and server, start an isolated localhost test with one command:

```bash
./run-tes3mp-local.sh
```

The launcher synchronizes CoreScripts, creates persistent client and server test state under `.tes3mp-test`, starts the server on `127.0.0.1:25565`, launches the client without the single-player intro and chargen sequence, and stops the server when the game closes. A new test client profile is seeded from the current OpenMW profile so its display, input, camera, and Lua settings are preserved. On the first run the launcher also detects the Morrowind data directory from OpenMW's configuration or asks for it and remembers the answer. Either source can be supplied explicitly:

```bash
./run-tes3mp-local.sh \
    --data-dir "/path/to/Morrowind/Data Files" \
    --client-profile "/path/to/existing/openmw/profile" \
    --server-profile "/path/to/existing/corescripts/server"
```

To run only the isolated server, use `./run-tes3mp-server.sh`. Run either script with `--help` for configuration overrides. Start with identical vanilla content and load order on both sides before adding mods.

Alpha servers refuse non-loopback listen addresses by default. An operator who deliberately exposes a server must set both `localAddress` to the desired numeric address and `publicListen = true` in the `[General]` section of `tes3mp-server.cfg`; firewall and network exposure remain the operator's responsibility.

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

By participating in the project, contributors agree to follow the [Code of Conduct](CODE_OF_CONDUCT.md). Report security issues through the private process in [SECURITY.md](SECURITY.md), not a public issue.

Credits and licenses
--------------------

TES3MP was founded and developed by David Cernat and Stanislav Zhukov on top of OpenMW. See [tes3mp-credits.md](tes3mp-credits.md) and [AUTHORS.md](AUTHORS.md) for the full contributor lists.

Font licenses:

* DejaVuLGCSansMono.ttf: custom; see [DejaVuFontLicense.txt](files/data/fonts/DejaVuFontLicense.txt).
* DemonicLetters.ttf: SIL Open Font License; see [DemonicLettersFontLicense.txt](files/data/fonts/DemonicLettersFontLicense.txt).
* MysticCards.ttf: SIL Open Font License; see [MysticCardsFontLicense.txt](files/data/fonts/MysticCardsFontLicense.txt).
