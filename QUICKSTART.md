# TES3MP Merged Branch - Quick Start Guide

This guide will help you quickly build and run the TES3MP merged branch (TES3MP 0.8.1 + openmw-50).

## Prerequisites

- Linux system (Debian/Ubuntu, Arch, Fedora, or similar)
- Git
- Sudo access (for installing dependencies)
- At least 4GB RAM and 10GB free disk space

## Quick Build (5 Minutes)

### 1. Build Everything

```bash
# From the project root directory
./tes3mp-merged-build.sh --install
```

This will:
- Install all required system dependencies
- Build CrabNet (TES3MP's RakNet fork - networking library)
- Build TES3MP client and server
- Save build log to `build.log`

**Note:** CrabNet is already cloned to `dependencies/crabnet/` and will be built automatically.

### 2. Run the Client

```bash
cd build
./tes3mp
```

### 3. Run the Server

```bash
cd build
./tes3mp-server
```

## Common Build Scenarios

### Server-Only Build

If you only need the server (no GUI):

```bash
./tes3mp-merged-build.sh --install --server-only
```

### Rebuild After Code Changes

```bash
./tes3mp-merged-build.sh --rebuild
```

### Use Specific Number of CPU Cores

```bash
# Use 4 cores (faster on systems with limited RAM)
./tes3mp-merged-build.sh --install --cores 4
```

### Skip Dependency Installation

If you already have dependencies installed:

```bash
./tes3mp-merged-build.sh --install --skip-pkgs
```

### Clean Build

To start fresh:

```bash
./tes3mp-merged-build.sh --clean
./tes3mp-merged-build.sh --install
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
   ./tes3mp-merged-build.sh --rebuild
   ```

### CrabNet (RakNet) Build Fails

```bash
# Rebuild CrabNet manually
cd dependencies/crabnet/build
rm -rf *
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCRABNET_ENABLE_DLL=OFF \
      -DCRABNET_ENABLE_SAMPLES=OFF \
      -DCRABNET_ENABLE_STATIC=ON \
      ..
make -j$(nproc)
```

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
- `tes3mp` - Main game client
- `tes3mp-server` - Multiplayer server
- `tes3mp-browser` - Server browser
- `openmw-launcher` - Game launcher (if not server-only)
- `openmw-cs` - Construction Set (if built)

**In `dependencies/` directory:**
- `crabnet/` - CrabNet (RakNet fork) networking library

## Next Steps

1. **Configure the client**: Copy Morrowind data files to the appropriate location
2. **Configure the server**: Edit server configuration files
3. **Join or host**: Use the browser to find servers or host your own

## Getting Help

- **Build Issues**: Check `build.log` for errors
- **Full Documentation**: See `BUILD_INSTRUCTIONS.md`
- **TES3MP Community**: [Discord](https://discord.gg/ECJk293)
- **OpenMW Documentation**: [openmw.readthedocs.io](https://openmw.readthedocs.io/)

## Merge Information

This build combines:
- **TES3MP 0.8.1**: Multiplayer features (154 files in mwmp/)
- **openmw-50**: Modern codebase with Lua scripting, updated navigation, ESM::RefId API

All 237 merge conflicts were resolved, preserving both codebases' features.

---

**Build Script Version**: 1.0.0-merged  
**Last Updated**: February 2026
