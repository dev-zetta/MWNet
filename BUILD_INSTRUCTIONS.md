# Building TES3MP 1.0.0-alpha.1 (OpenMW 0.52)

This document provides build instructions for the in-progress TES3MP 1.0.0 release, based on OpenMW 0.52.

## Project Information

- **TES3MP Version:** 1.0.0-alpha.1
- **OpenMW Base:** 0.52.0
- **Branch:** tes3mp_merged
- **C++ Standard:** C++20
- **CMake Requirement:** 3.16.0 or higher

This maintained fork combines TES3MP's multiplayer features with OpenMW 0.52's modernized codebase.

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
- Resolves the pinned GameNetworkingSockets transport and system libsodium dependency
- Configures CMake with correct parameters for the merged branch
- Uses the C++20 standard required by the current OpenMW base
- Saves build log to `build.log`
- Supports both full build and server-only configurations

**Skip options:**
```bash
# Skip package installation (if dependencies already installed)
./tes3mp-merged-build.sh --install --skip-pkgs

# Require already installed dependencies instead of using the pinned fetch fallback
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
- Qt 6 (for the launcher and editor tools)
- MyGUI
- FFmpeg (libavcodec, libavformat, libavutil, libswscale)
- OpenAL
- Bullet Physics
- LuaJIT or Lua 5.1
- LZ4
- RecastNavigation

TES3MP-specific:
- **GameNetworkingSockets v1.5.1** for the encrypted transport. Install a CMake
  package or use `-DTES3MP_FETCH_DEPS=ON` to fetch the revision recorded in
  `DEPENDENCIES.md`.
- **libsodium 1.0.18 or newer** for server identities, authenticated sessions
  and Argon2id password hashing.
- OpenSSL and Protobuf are required when building the pinned
  GameNetworkingSockets fallback.

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
  librecast-dev \
  libsodium-dev libssl-dev libprotobuf-dev protobuf-compiler
```

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
  -DBUILD_LAUNCHER=ON \
  -DBUILD_OPENCS=ON \
  -DTES3MP_FETCH_DEPS=ON
```

Omit `TES3MP_FETCH_DEPS` when an approved GameNetworkingSockets CMake package is installed.

**Available CMake Options:**

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_OPENMW` | ON | Build the main TES3MP client |
| `BUILD_OPENMW_MP` | ON | Build TES3MP server (154 multiplayer files) |
| `BUILD_LAUNCHER` | ON | Build game launcher |
| `BUILD_OPENCS` | ON | Build OpenMW Construction Set |
| `BUILD_WIZARD` | ON | Build installation wizard |
| `BUILD_MWINIIMPORTER` | ON | Build Morrowind.ini importer |
| `BUILD_ESSIMPORTER` | ON | Build savegame importer |
| `BUILD_BSATOOL` | ON | Build BSA archive tool |
| `BUILD_ESMTOOL` | ON | Build ESM file inspector |
| `BUILD_COMPONENTS_TESTS` | OFF | Build component tests |
| `BUILD_BENCHMARKS` | OFF | Build benchmarks |
| `BUILD_TES3MP_TESTS` | OFF | Build TES3MP protocol, transport and server tests |
| `BUILD_TES3MP_FUZZERS` | OFF | Build TES3MP libFuzzer targets |
| `TES3MP_TESTS_ONLY` | OFF | Configure the dependency-light protocol and server test tree only |
| `TES3MP_TESTS_WITH_SECURITY` | OFF | Add libsodium and Boost-based authentication and handshake coverage to a tests-only build |
| `TES3MP_FETCH_DEPS` | OFF | Fetch the pinned GameNetworkingSockets revision when no package is installed |

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
├── openmw-launcher     # Game launcher
├── openmw-cs           # Construction Set
└── ... (other tools)
```

---

## Testing and release validation

The protocol, persistence, mechanics and ownership tests have a dependency-light configuration that does not require the OpenMW client stack:

```bash
cmake -S . -B build-protocol -DTES3MP_TESTS_ONLY=ON
cmake --build build-protocol --parallel
ctest --test-dir build-protocol --output-on-failure
```

A full dependency build with `BUILD_TES3MP_TESTS=ON` also provides `tes3mp-headless-integration`, which exercises the authenticated protocol over real encrypted loopback connections, and `tes3mp-persistence-fault`, which kills a writer process at every atomic-save stage.

The release-budget fuzz campaign requires Clang/libFuzzer, libsodium and the Boost headers. A minimal build of all four targets can be configured without the OpenMW client dependencies:

```bash
CC=clang CXX=clang++ cmake -S . -B build-fuzz \
    -DTES3MP_TESTS_ONLY=ON \
    -DTES3MP_TESTS_WITH_SECURITY=ON \
    -DBUILD_TES3MP_FUZZERS=ON
cmake --build build-fuzz --parallel
```

The campaign runs the protocol, transport, authentication and secure-handshake targets concurrently and retains their corpora, logs, failures and an exact-commit campaign manifest:

```bash
CI/run_tes3mp_fuzz_campaign.sh --release-budget
```

Run the mandatory connect/death cycles and 24-hour eight-client latency/loss soak against an exact candidate with:

```bash
CI/run_tes3mp_soak.sh --release-gates
```

To run the same release soak under ASan, LeakSanitizer and UBSan in an isolated
container, build the regular dependency image followed by the sanitizer target:

```bash
docker build -f Dockerfile.tes3mp -t tes3mp-build:alpha1-sanitizers .
test -z "$(git status --porcelain)"
source_commit="$(git rev-parse HEAD)"
docker build --build-arg TES3MP_SOURCE_COMMIT="$source_commit" \
    -f Dockerfile.tes3mp-sanitizer -t tes3mp-soak:alpha1-sanitizers .
docker run --name tes3mp-alpha1-sanitizer-soak \
    tes3mp-soak:alpha1-sanitizers --release-gates
```

Do not use `--rm`: the completed `/artifacts` directory must remain available
for `docker cp` and review. The sanitizer image runs the unit, persistence-fault
and encrypted headless tests while it is built, before the long soak can start.
TES3MP calls the pinned GNS build through its flat ABI because GNS disables
RTTI. The fetched GNS sources retain ASan but are excluded from UBSan because
v1.5.1 deliberately erases callback types and uses unaligned packet-buffer
access; TES3MP sources retain both ASan and UBSan.

Compare like-for-like performance artifacts against the recorded alpha.1 baseline. A regression above five percent requires an explicit reviewed justification:

```bash
CI/compare_tes3mp_performance.py baseline.json candidate.json
```

Shorter developer runs are available through each script's `--help`, but do not satisfy the release gates. See [RELEASE_GATES.md](RELEASE_GATES.md) for all cross-platform, sanitizer, fuzz, soak, security and legal evidence required before a stable release.

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

---

## Important Notes for This Merged Branch

### API Changes

The original integration updated TES3MP's code to the OpenMW 0.50 APIs. The current branch advances that work to OpenMW 0.52:
- **String to RefId:** All ID parameters changed from `std::string` to `ESM::RefId`
- **Navigation:** Uses OpenMW's updated detournavigator with `ObjectTransform`
- **Lighting:** Settings-based lighting method configuration

### Potential Build Issues

1. **Missing transport dependencies:** Install GameNetworkingSockets and libsodium,
   or enable the pinned GameNetworkingSockets fallback with
   `-DTES3MP_FETCH_DEPS=ON`. The fallback still requires libsodium, OpenSSL and
   Protobuf development packages.

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

**From OpenMW 0.52:**
- Lua scripting system (apps/openmw/mwlua/)
- Modern navigation system (detournavigator)
- Post-processing pipeline
- ESM::RefId API throughout
- Updated build system with library structure

**From the TES3MP multiplayer lineage:**
- Multiplayer core (apps/openmw/mwmp/ - 154 files)
- In-game saved/recent direct connect with encrypted probes and TOFU fingerprints
- Encrypted GameNetworkingSockets transport
- 144 files with multiplayer additions marked

The original OpenMW 0.50 integration resolved 237 merge conflicts while preserving TES3MP's multiplayer features; the codebase was subsequently advanced to OpenMW 0.52.

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
