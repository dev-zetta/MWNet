# Migrating to MWNet

MWNet is an independent fork of TES3MP and OpenMW. It has its own executable names, configuration, Lua API namespace, and protocol identity. The original projects' authorship and license notices remain in the source distribution.

## Build and launch

Build the `mwnet`, `mwnet-server`, and `mwnet-directory` targets. Build options use `MWNET_` or `BUILD_MWNET_` prefixes, such as `MWNET_FETCH_DEPS`, `MWNET_TESTS_ONLY`, and `BUILD_MWNET_TESTS`. Run `./run-mwnet-local.sh` for a local game, `./run-mwnet-server.sh` for a dedicated server, or `./run-mwnet.sh` for the client. Container builds use `Dockerfile.mwnet` and `Dockerfile.mwnet-sanitizer`.

## Configuration and saved state

The client and server read `mwnet-client.cfg` and `mwnet-server.cfg`; the shipped templates are `mwnet-client-default.cfg` and `mwnet-server-default.cfg`. Local launchers create isolated state under `.mwnet-test/` and accept `MWNET_*` environment overrides documented by `--help`. OpenMW engine settings and its XDG configuration directory retain their engine names.

Existing pre-rebrand profiles are not automatically overwritten or migrated. Back up the previous state first. Use `--client-profile` and `--server-profile` with `run-mwnet-local.sh`, or `MWNET_SERVER_PROFILE` with `run-mwnet-server.sh`, to import a selected profile into fresh local state. Review multiplayer configuration manually under the new filenames. Build into a fresh directory, or reconfigure an existing build with the new options; old cached build options do not enable the renamed targets.

## Server scripts

The server API is now the global Lua table `mwnet`; for example, `mwnet.GetServerVersion()` returns the server version. All bundled CoreScripts and regression fixtures use this table. Update third-party scripts to refer to `mwnet` before loading them. The old global table is not aliased. Function and callback names are compared with the immutable upstream 0.8.1 baseline by `CI/check_mwnet_lua_api.sh`; this does not imply that unchanged legacy script files can run. See [Lua API compatibility](../LUA_API_COMPATIBILITY.md).

## Network and discovery

MWNet uses gameplay protocol 13 and MWNet-specific authentication and discovery signature domains. Pre-rebrand clients, servers, and directory services are incompatible. Upgrade and rebuild all peers together. Discovery API v1 retains its JSON structure but requires the new signatures. No public domain is configured or registered by this rename; set your own HTTPS directory origin when deploying.

## Historical validation

The records under `docs/release-evidence/` describe earlier source commits and binaries. They establish development history, not acceptance of the renamed build. Historical labels are marked separately where normalized; original records remain accessible at their recorded source revision or in Git history. Current validation must use the renamed executables and protocol.
