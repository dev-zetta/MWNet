# MWNet 1.0.0-alpha.2 - Quick Start Guide

This guide covers the MWNet 1.0.0-alpha.2 development candidate, based on OpenMW 0.52.
It is not a stable release; see [release gates](RELEASE_GATES.md) for outstanding validation.

## Prerequisites

- Linux system (Debian/Ubuntu, Arch, Fedora, or similar)
- Git
- Sudo access (for installing dependencies)
- At least 4GB RAM and 10GB free disk space

## Build

### 1. Build Everything

```bash
# From the project root directory
./mwnet-merged-build.sh --install
```

This will:
- Install all required system dependencies
- Resolve GameNetworkingSockets at the pinned revision and use the system libsodium
- Build MWNet client and server
- Save build log to `build.log`

### 2. Run a Local Multiplayer Test

```bash
./run-mwnet-local.sh
```

This starts an isolated localhost server and the game together, bypassing the single-player intro and character-generation sequence before login. The first run copies display, input, camera, and Lua settings from your current OpenMW profile, asks for the Morrowind `Data Files` directory if it cannot be detected, then remembers both for later runs. Use `--client-profile PATH` to seed the test from another existing profile, or `--server-profile PATH` to migrate an existing CoreScripts server's accounts and world state.

### 3. Run Only the Client or Server

```bash
./run-mwnet.sh
./run-mwnet-server.sh
```

## Common Build Scenarios

### Server-Only Build

If you only need the server (no GUI):

```bash
./mwnet-merged-build.sh --install --server-only
```

### Rebuild After Code Changes

```bash
./mwnet-merged-build.sh --rebuild
```

### Use Specific Number of CPU Cores

```bash
# Use 4 cores (faster on systems with limited RAM)
./mwnet-merged-build.sh --install --cores 4
```

### Skip Dependency Installation

If you already have dependencies installed:

```bash
./mwnet-merged-build.sh --install --skip-pkgs
```

### Clean Build

To start fresh:

```bash
./mwnet-merged-build.sh --clean
./mwnet-merged-build.sh --install
```

## Troubleshooting

### Build Fails with Dependency Errors

1. Check the build log:
   ```bash
   cat build.log | grep -i error
   ```

2. Install missing dependencies manually:
   ```bash
   # Debian/Ubuntu
   sudo apt install <package-name>
   
   # Arch
   sudo pacman -S <package-name>
   
   # Fedora
   sudo dnf install <package-name>
   ```

3. Rebuild:
   ```bash
   ./mwnet-merged-build.sh --rebuild
   ```

### GameNetworkingSockets Is Not Found

```bash
# Use the reviewed pinned source fallback
cmake -S . -B build -DMWNET_FETCH_DEPS=ON
cmake --build build
```

The fallback requires Git, OpenSSL and Protobuf development packages. libsodium
is always a required system or toolchain dependency.

### CMake Configuration Errors

View detailed CMake output:

```bash
cd build
cmake .. -LAH | less
```

### Compilation Errors

Build with verbose output:

```bash
cd build
make VERBOSE=1 2>&1 | tee verbose_build.log
```

## What Was Built?

After successful build, you'll have:

**In `build/` directory:**
- `mwnet` - Main game client
- `mwnet-server` - Multiplayer server
- `openmw-launcher` - Game launcher (if not server-only)
- `openmw-cs` - Construction Set (if built)

**In `dependencies/` directory:**
- GameNetworkingSockets and libsodium are resolved as MWNet dependencies.

## Next Steps

1. **Configure the client**: Copy Morrowind data files to the appropriate location
2. **Configure the server**: Edit server configuration files
3. **Join or host**: Use the in-game direct-connect screen or host your own server

## Getting Help

- **Build Issues**: Check `build.log` for errors
- **Full Documentation**: See `BUILD_INSTRUCTIONS.md`
- **MWNet Community**: [Discord](https://discord.gg/ECJk293)
- **OpenMW Documentation**: [openmw.readthedocs.io](https://openmw.readthedocs.io/)

## Merge Information

This build combines:
- **MWNet 1.0.0-alpha.2**: Multiplayer client, dedicated server, direct connect and scripting API
- **OpenMW 0.52**: Current engine base with modern Lua, navigation, rendering and content APIs

The original OpenMW 0.50 integration resolved 237 merge conflicts while preserving MWNet's multiplayer features; the codebase was subsequently advanced to OpenMW 0.52.

---

**Build Script Version**: 1.0.0
**Last Updated**: August 2026
