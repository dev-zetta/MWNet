# Building MWNet 1.0.0-alpha.1 (OpenMW 0.52)

This document provides build instructions for the in-progress MWNet 1.0.0 release, based on OpenMW 0.52.

## Project Information

- **MWNet Version:** 1.0.0-alpha.1
- **OpenMW Base:** 0.52.0
- **Branch:** mwnet_merged
- **C++ Standard:** C++20
- **CMake Requirement:** 3.16.0 or higher

This maintained fork combines MWNet's multiplayer features with OpenMW 0.52's modernized codebase.

---

## Build Options

### Option 1: Docker Build (Recommended - No Dependency Issues)

Build in an isolated Docker container with all dependencies pre-installed:

```bash
# Build the Docker image (first time only)
docker build -f Dockerfile.mwnet -t mwnet-merged:latest .

# Build MWNet in Docker
docker run --rm -v "$(pwd)":/mwnet:Z -e NPROC=$(nproc) mwnet-merged:latest
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
chmod +x mwnet-merged-build.sh

# View available options
./mwnet-merged-build.sh --help

# Install and build everything (client + server + dependencies)
./mwnet-merged-build.sh --install

# Build server-only
./mwnet-merged-build.sh --install --server-only

# Specify number of CPU cores
./mwnet-merged-build.sh --install --cores 4

# Rebuild after making code changes
./mwnet-merged-build.sh --rebuild

# Clean build directory
./mwnet-merged-build.sh --clean
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
./mwnet-merged-build.sh --install --skip-pkgs

# Require already installed dependencies instead of using the pinned fetch fallback
./mwnet-merged-build.sh --install --skip-deps
```

---

### Option 3: Original MWNet-deploy Script (Advanced)

The original MWNet community deployment script is available but requires modification for this merged branch:

```bash
# The script is already cloned in MWNet-deploy/
cd MWNet-deploy

# Note: This script downloads from the official MWNet repository
# You would need to modify it to use your local merged branch
./mwnet-deploy.sh --install
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

MWNet-specific:
- **GameNetworkingSockets v1.5.1** for the encrypted transport. Install a CMake
  package or use `-DMWNET_FETCH_DEPS=ON` to fetch the revision recorded in
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
cd /home/gmax/dev/MWNet
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
  -DMWNET_FETCH_DEPS=ON
```

Omit `MWNET_FETCH_DEPS` when an approved GameNetworkingSockets CMake package is installed.

**Available CMake Options:**

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_OPENMW` | ON | Build the main MWNet client |
| `BUILD_OPENMW_MP` | ON | Build MWNet server (154 multiplayer files) |
| `BUILD_LAUNCHER` | ON | Build game launcher |
| `BUILD_OPENCS` | ON | Build OpenMW Construction Set |
| `BUILD_WIZARD` | ON | Build installation wizard |
| `BUILD_MWINIIMPORTER` | ON | Build Morrowind.ini importer |
| `BUILD_ESSIMPORTER` | ON | Build savegame importer |
| `BUILD_BSATOOL` | ON | Build BSA archive tool |
| `BUILD_ESMTOOL` | ON | Build ESM file inspector |
| `BUILD_COMPONENTS_TESTS` | OFF | Build component tests |
| `BUILD_BENCHMARKS` | OFF | Build benchmarks |
| `BUILD_MWNET_TESTS` | OFF | Build MWNet protocol, transport and server tests |
| `BUILD_MWNET_FUZZERS` | OFF | Build MWNet libFuzzer targets |
| `MWNET_TESTS_ONLY` | OFF | Configure the dependency-light protocol and server test tree only |
| `MWNET_TESTS_WITH_SECURITY` | OFF | Add libsodium and Boost-based authentication and handshake coverage to a tests-only build |
| `MWNET_FETCH_DEPS` | OFF | Fetch the pinned GameNetworkingSockets revision when no package is installed |

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
├── mwnet              # Main MWNet client executable
├── mwnet-server       # MWNet multiplayer server
├── openmw-launcher     # Game launcher
├── openmw-cs           # Construction Set
└── ... (other tools)
```

---

## Testing and release validation

The protocol, persistence, mechanics and ownership tests have a dependency-light configuration that does not require the OpenMW client stack:

```bash
cmake -S . -B build-protocol -DMWNET_TESTS_ONLY=ON
cmake --build build-protocol --parallel
ctest --test-dir build-protocol --output-on-failure
```

A full dependency build with `BUILD_MWNET_TESTS=ON` also provides `mwnet-headless-integration`, which exercises the authenticated protocol over real encrypted loopback connections, and `mwnet-persistence-fault`, which kills a writer process at every atomic-save stage.

The release-budget fuzz campaign requires Clang/libFuzzer, libsodium and the Boost headers. A minimal build of all four targets can be configured without the OpenMW client dependencies:

```bash
CC=clang CXX=clang++ cmake -S . -B build-fuzz \
    -DMWNET_TESTS_ONLY=ON \
    -DMWNET_TESTS_WITH_SECURITY=ON \
    -DBUILD_MWNET_FUZZERS=ON
cmake --build build-fuzz --parallel
```

The campaign runs the protocol, transport, authentication and secure-handshake targets concurrently and retains their corpora, logs, failures and an exact-commit campaign manifest:

```bash
CI/run_mwnet_fuzz_campaign.sh --release-budget
```

Run the mandatory connect/death cycles and paired 24-hour eight-client latency/loss
soak against an exact candidate with a native build in `build` and an ASan/UBSan
build in `build-sanitizer`:

```bash
CI/run_mwnet_soak.sh --release-gates
```

The native process must pass the 1% post-warm-up RSS limit. The second process
must finish the same workload with ASan, LeakSanitizer and UBSan enabled; its
allocator and stack-history RSS is recorded separately. Both must pass.
RSS samples are buffered to each profile's `state/resident-memory-samples.bin`
and streamed into the final JSON report, so the observer's memory does not
grow with the run's cycle count. Keep artifact storage writable and allow
eight bytes per cycle per profile for this internal journal.
Interrupted scenarios retain partial JSON with `status` and `scenariosComplete`;
these reports are diagnostic and cannot pass the paired gate. On Linux hosts
with both Docker Desktop and Docker Engine, `docker --context default` selects
the native engine. Build/load and run the image in the same selected context.
`--sanitizer-build-dir DIR` selects a different sanitizer build, and also enables
paired shorter developer runs without `--release-gates`.

To build both profiles and run the paired gate in one isolated container:

```bash
docker build -f Dockerfile.mwnet -t mwnet-build:alpha1-sanitizers .
git diff --quiet HEAD --
source_commit="$(git rev-parse HEAD)"
git archive --format=tar HEAD | docker build \
    --build-arg MWNET_SOURCE_COMMIT="$source_commit" \
    -f Dockerfile.mwnet-sanitizer -t mwnet-soak:alpha1-sanitizers -
docker run -d --name mwnet-alpha1-sanitizer-soak \
    --memory 2g --memory-swap 2g \
    mwnet-soak:alpha1-sanitizers --release-gates
```

Do not use `--rm`: the completed `/artifacts` directory must remain available
for `docker cp` and review. `/artifacts/manifest.json` records both exit codes,
executable/artifact hashes and memory limits. Each profile retains its own
`metrics.json`, `soak.log` and state under `native/` or `sanitizer/`. A failed
process stops its sibling and the overall gate fails. The image runs unit,
persistence-fault and encrypted headless tests for both builds before the
long soak can start. The archive includes only committed candidate sources.

Allow 2 GiB for the paired runtime; the runner rejects a lower cgroup limit for
a release run. The observed individual peaks were about 734 MiB with ASan and
269 MiB without it. Building the images requires additional memory. Python 3
is required by the paired runner and is included in the dependency image.
MWNet calls the pinned GNS build through its flat ABI because GNS disables
RTTI. In the sanitizer profile, fetched GNS sources retain ASan but are excluded
from UBSan because
v1.5.1 deliberately erases callback types and uses unaligned packet-buffer
access; MWNet sources retain both ASan and UBSan.

Compare like-for-like performance artifacts against the recorded alpha.1 baseline. A regression above five percent requires an explicit reviewed justification:

```bash
CI/compare_mwnet_performance.py baseline.json candidate.json
```

Shorter developer runs are available through each script's `--help`, but do not satisfy the release gates. See [RELEASE_GATES.md](RELEASE_GATES.md) for all cross-platform, sanitizer, fuzz, soak, security and legal evidence required before a stable release.

---

## Running MWNet

### Client

```bash
cd build
./mwnet
```

### Server

```bash
cd build
./mwnet-server
```

---

## Important Notes for This Merged Branch

### API Changes

The original integration updated MWNet's code to the OpenMW 0.50 APIs. The current branch advances that work to OpenMW 0.52:
- **String to RefId:** All ID parameters changed from `std::string` to `ESM::RefId`
- **Navigation:** Uses OpenMW's updated detournavigator with `ObjectTransform`
- **Lighting:** Settings-based lighting method configuration

### Potential Build Issues

1. **Missing transport dependencies:** Install GameNetworkingSockets and libsodium,
   or enable the pinned GameNetworkingSockets fallback with
   `-DMWNET_FETCH_DEPS=ON`. The fallback still requires libsodium, OpenSSL and
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

**From the MWNet multiplayer lineage:**
- Multiplayer core (apps/openmw/mwmp/ - 154 files)
- In-game saved/recent direct connect with encrypted probes and TOFU fingerprints
- Encrypted GameNetworkingSockets transport
- 144 files with multiplayer additions marked

The original OpenMW 0.50 integration resolved 237 merge conflicts while preserving MWNet's multiplayer features; the codebase was subsequently advanced to OpenMW 0.52.

---

## Additional Resources

- [OpenMW Documentation](https://openmw.readthedocs.io/)
- [MWNet Discord](https://discord.gg/ECJk293)
- [MWNet Forum](https://forum.openmw.org/viewforum.php?f=45)

---

## License

- MWNet: GPLv3 with additional allowed terms
- OpenMW: GPLv3

See [LICENSE](LICENSE) for full details.
