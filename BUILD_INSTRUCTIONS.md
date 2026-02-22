# Building TES3MP (Merged with openmw-50)

This document provides build instructions for the TES3MP project that has been merged with openmw-50.

## Project Information

- **TES3MP Version:** 0.8.1
- **OpenMW Base:** 0.50.0
- **Branch:** tes3mp_merged
- **C++ Standard:** C++20
- **CMake Requirement:** 3.16.0 or higher

This is a custom merged branch combining TES3MP's multiplayer features with openmw-50's modernized codebase.

---

## Build Options

### Option 1: Docker Build (Recommended - No Dependency Issues)

Build in an isolated Docker container with all dependencies pre-installed:

```bash
# Build the Docker image (first time only)
docker build -f Dockerfile.tes3mp -t tes3mp-merged:latest .

# Build TES3MP in Docker
docker run --rm -v "$(pwd)":/tes3mp:Z -e NPROC=$(nproc) tes3mp-merged:latest
```

**Advantages:**
- No dependency conflicts with your system
- Reproducible builds
- Works on any Linux distribution
- Easy cleanup (just remove the image)
- No need to install packages on host

See [DOCKER_BUILD.md](DOCKER_BUILD.md) for detailed Docker build instructions.

---

### Option 2: Custom Build Script (Native Build)

A custom build script has been created specifically for this merged branch. It handles dependencies and builds the project with the correct configuration:

```bash
# Make the script executable (if not already)
chmod +x tes3mp-merged-build.sh

# View available options
./tes3mp-merged-build.sh --help

# Install and build everything (client + server + dependencies)
./tes3mp-merged-build.sh --install

# Build server-only
./tes3mp-merged-build.sh --install --server-only

# Specify number of CPU cores
./tes3mp-merged-build.sh --install --cores 4

# Rebuild after making code changes
./tes3mp-merged-build.sh --rebuild

# Clean build directory
./tes3mp-merged-build.sh --clean
```

**Features:**
- Automatically detects your Linux distribution and installs dependencies
- Builds CrabNet (TES3MP's networking library)
- Configures CMake with correct parameters for the merged branch
- Uses C++20 standard (required for openmw-50)
- Saves build log to `build.log`
- Supports both full build and server-only configurations

**Skip options:**
```bash
# Skip package installation (if dependencies already installed)
./tes3mp-merged-build.sh --install --skip-pkgs

# Skip CrabNet build (if already built)
./tes3mp-merged-build.sh --install --skip-deps
```

---

### Option 3: Original TES3MP-deploy Script (Advanced)

The original TES3MP community deployment script is available but requires modification for this merged branch:

```bash
# The script is already cloned in TES3MP-deploy/
cd TES3MP-deploy

# Note: This script downloads from the official TES3MP repository
# You would need to modify it to use your local merged branch
./tes3mp-deploy.sh --install
```

**Note:** The custom script (Option 2) or Docker (Option 1) are recommended as they're already configured for this merged branch.

---

### Option 4: Manual Build with CMake

#### Prerequisites

**System Requirements:**
- CMake 3.16 or higher
- C++20 compatible compiler:
  - GCC 10+ or
  - Clang 11+
- Git (for version information)

**Required Dependencies:**

Core libraries:
- Boost (filesystem, program_options, system, iostreams)
- SDL2
- OpenSceneGraph (OSG)
- Qt5 (for launcher and browser)
- MyGUI
- FFmpeg (libavcodec, libavformat, libavutil, libswscale)
- OpenAL
- Bullet Physics
- LuaJIT or Lua 5.1
- LZ4
- RecastNavigation

TES3MP-specific:
- **CrabNet** (TES3MP's fork of RakNet - networking library for multiplayer functionality)
  - Repository: https://github.com/TES3MP/CrabNet
  - Already cloned to `dependencies/crabnet/`
  - Requires CMake 3.5+ and C++11

**On Debian/Ubuntu:**

```bash
sudo apt update
sudo apt install -y \
  cmake build-essential git \
  libboost-all-dev \
  libsdl2-dev \
  qt5-default qtbase5-dev qttools5-dev \
  libopenscenegraph-dev \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
  libmygui-dev \
  libbullet-dev \
  libopenal-dev \
  libunshield-dev \
  liblz4-dev \
  libluajit-5.1-dev \
  librecast-dev
```

**CrabNet (RakNet) Installation:**

CrabNet is already cloned to `dependencies/crabnet/`. To build it manually:

```bash
cd dependencies/crabnet
mkdir -p build && cd build

cmake -DCMAKE_BUILD_TYPE=Release \
      -DCRABNET_ENABLE_DLL=OFF \
      -DCRABNET_ENABLE_SAMPLES=OFF \
      -DCRABNET_ENABLE_STATIC=ON \
      ..

make -j$(nproc)
```

The static library will be built at: `dependencies/crabnet/build/lib/libRakNetLibStatic.a`

#### Build Steps

1. **Navigate to project directory:**

```bash
cd /home/gmax/dev/TES3MP
```

2. **Create build directory:**

```bash
mkdir build
cd build
```

3. **Configure with CMake:**

```bash
cmake .. \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_STANDARD=20 \
  -DBUILD_OPENMW=ON \
  -DBUILD_OPENMW_MP=ON \
  -DBUILD_BROWSER=ON \
  -DBUILD_LAUNCHER=ON \
  -DBUILD_OPENCS=ON \
  -DBUILD_MASTER=OFF \
  -DRakNet_INCLUDES=/home/gmax/dev/TES3MP/dependencies/crabnet/include \
  -DRakNet_LIBRARY_DEBUG=/home/gmax/dev/TES3MP/dependencies/crabnet/build/lib/libRakNetLibStatic.a \
  -DRakNet_LIBRARY_RELEASE=/home/gmax/dev/TES3MP/dependencies/crabnet/build/lib/libRakNetLibStatic.a
```

**Note:** The CrabNet (RakNet) paths are required for TES3MP's multiplayer functionality.

**Available CMake Options:**

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_OPENMW` | ON | Build the main TES3MP client |
| `BUILD_OPENMW_MP` | ON | Build TES3MP server (154 multiplayer files) |
| `BUILD_BROWSER` | ON | Build server browser (22 files) |
| `BUILD_MASTER` | OFF | Build master server (9 files) |
| `BUILD_LAUNCHER` | ON | Build game launcher |
| `BUILD_OPENCS` | ON | Build OpenMW Construction Set |
| `BUILD_WIZARD` | ON | Build installation wizard |
| `BUILD_MWINIIMPORTER` | ON | Build Morrowind.ini importer |
| `BUILD_ESSIMPORTER` | ON | Build savegame importer |
| `BUILD_BSATOOL` | ON | Build BSA archive tool |
| `BUILD_ESMTOOL` | ON | Build ESM file inspector |
| `BUILD_COMPONENTS_TESTS` | OFF | Build component tests |
| `BUILD_BENCHMARKS` | OFF | Build benchmarks |

**Build Types:**
- `Debug` - No optimization, full debug symbols
- `Release` - Full optimization, no debug symbols
- `RelWithDebInfo` - Optimized with debug symbols (recommended)
- `MinSizeRel` - Optimized for size

4. **Build the project:**

```bash
# Use all available CPU cores
make -j$(nproc)

# Or specify core count (e.g., 4 cores)
make -j4

# Save build output to log file
make -j$(nproc) 2>&1 | tee build.log
```

5. **Install (optional):**

```bash
sudo make install
```

---

## Build Output

After successful compilation, executables will be located in:

```
build/
├── tes3mp              # Main TES3MP client executable
├── tes3mp-server       # TES3MP multiplayer server
├── tes3mp-browser      # Server browser
├── openmw-launcher     # Game launcher
├── openmw-cs           # Construction Set
└── ... (other tools)
```

---

## Running TES3MP

### Client

```bash
cd build
./tes3mp
```

### Server

```bash
cd build
./tes3mp-server
```

### Server Browser

```bash
cd build
./tes3mp-browser
```

---

## Important Notes for This Merged Branch

### API Changes

This merge updated TES3MP's code to use openmw-50's modern API:
- **String to RefId:** All ID parameters changed from `std::string` to `ESM::RefId`
- **Navigation:** Uses openmw-50's updated detournavigator with `ObjectTransform`
- **Lighting:** Settings-based lighting method configuration

### Potential Build Issues

1. **Missing CrabNet:** TES3MP requires CrabNet for networking. Ensure it's properly installed and linked.

2. **API Mismatches:** Watch for compilation errors related to:
   - Type conversions between `std::string` and `ESM::RefId`
   - Missing header includes
   - Changed function signatures

3. **Linker Errors:** If you encounter undefined references, verify all dependencies are installed.

### Troubleshooting

**Check CMake configuration:**
```bash
cd build
cmake .. -LAH | grep -i "not found"
```

**View detailed build errors:**
```bash
make VERBOSE=1 2>&1 | tee build_verbose.log
```

**Clean build:**
```bash
cd build
rm -rf *
cmake ..
make -j$(nproc)
```

---

## Merge Information

This build includes:

**From openmw-50:**
- Lua scripting system (apps/openmw/mwlua/)
- Modern navigation system (detournavigator)
- Post-processing pipeline
- ESM::RefId API throughout
- Updated build system with library structure

**From TES3MP 0.8.1:**
- Multiplayer core (apps/openmw/mwmp/ - 154 files)
- Server browser (apps/browser/ - 22 files)
- Master server (apps/master/ - 9 files)
- CrabNet networking integration
- 144 files with multiplayer additions marked

All 237 merge conflicts were resolved, preserving both openmw-50's modernization and TES3MP's multiplayer features.

---

## Additional Resources

- [TES3MP Official Repository](https://github.com/TES3MP/TES3MP)
- [TES3MP Wiki](https://github.com/TES3MP/TES3MP/wiki)
- [TES3MP-deploy Script](https://github.com/GrimKriegor/TES3MP-deploy)
- [OpenMW Documentation](https://openmw.readthedocs.io/)
- [TES3MP Discord](https://discord.gg/ECJk293)
- [TES3MP Forum](https://forum.openmw.org/viewforum.php?f=45)

---

## License

- TES3MP: GPLv3 with additional allowed terms
- OpenMW: GPLv3

See [LICENSE](LICENSE) for full details.
