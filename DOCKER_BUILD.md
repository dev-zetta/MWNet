# Docker Build Guide for TES3MP Merged Branch

This guide explains how to build the TES3MP merged branch using Docker, which avoids dependency issues on your host system.

## Prerequisites

- Docker installed on your system
- At least 10GB free disk space
- The TES3MP merged branch repository

## Quick Start

### 1. Build the Docker Image

```bash
docker build -f Dockerfile.tes3mp -t tes3mp-merged:latest .
```

This creates a Docker image with all build dependencies pre-installed.

### 2. Build TES3MP in Docker

```bash
docker run --rm \
  -v "$(pwd)":/tes3mp:Z \
  -e NPROC=$(nproc) \
  tes3mp-merged:latest
```

**Options explained:**
- `--rm` - Remove container after build completes
- `-v "$(pwd)":/tes3mp:Z` - Mount current directory into container
- `-e NPROC=$(nproc)` - Use all available CPU cores
- `Z` flag - SELinux label for proper permissions

### 3. Find Your Executables

After the build completes, executables will be in:
```
build/tes3mp              # Client
build/tes3mp-server       # Server
```

## Advanced Usage

### Specify CPU Cores

```bash
docker run --rm \
  -v "$(pwd)":/tes3mp:Z \
  -e NPROC=4 \
  tes3mp-merged:latest
```

### Server-Only Build

Modify `docker-build.sh` to set:
```bash
-DBUILD_OPENMW=OFF \
-DBUILD_LAUNCHER=OFF
```

### Interactive Build (for debugging)

```bash
docker run --rm -it \
  -v "$(pwd)":/tes3mp:Z \
  --entrypoint /bin/bash \
  tes3mp-merged:latest
```

Then inside the container:
```bash
/docker-build.sh
```

## What Gets Built

The Docker build process:

1. **GameNetworkingSockets** (if not already available)
   - Resolved with `find_package`, or fetched at the pinned revision when enabled

2. **TES3MP Merged Branch**
   - Location: `build/`
   - Executables: `tes3mp`, `tes3mp-server`

## Advantages of Docker Build

✅ **Consistent environment** - Same build environment every time  
✅ **No host pollution** - Dependencies stay in container  
✅ **Easy cleanup** - Just remove the image  
✅ **Reproducible** - Works on any system with Docker  
✅ **No package conflicts** - Isolated from host packages  

## Troubleshooting

### Permission Issues

If you get permission errors, ensure the Z flag is used:
```bash
-v "$(pwd)":/tes3mp:Z
```

### Build Fails

Check the build log:
```bash
docker run --rm \
  -v "$(pwd)":/tes3mp:Z \
  -e NPROC=1 \
  tes3mp-merged:latest 2>&1 | tee docker-build.log
```

### Clean Build

Remove the build directory and rebuild:
```bash
rm -rf build
docker run --rm -v "$(pwd)":/tes3mp:Z tes3mp-merged:latest
```

### Update Docker Image

Rebuild the image to get latest dependencies:
```bash
docker build --no-cache -f Dockerfile.tes3mp -t tes3mp-merged:latest .
```

## Docker Image Details

**Base Image:** Ubuntu 22.04 LTS  
**Size:** ~2GB (with all dependencies)  
**Build Time:** ~5-10 minutes (first time)  
**Compile Time:** ~10-30 minutes (depending on CPU)

## Cleanup

### Remove Docker Image
```bash
docker rmi tes3mp-merged:latest
```

### Remove Build Artifacts
```bash
rm -rf build
```

## Comparison: Docker vs Native Build

| Aspect | Docker Build | Native Build |
|--------|--------------|--------------|
| Setup Time | 5-10 min (first time) | Varies by system |
| Dependencies | Isolated in container | Installed on host |
| Reproducibility | High | Medium |
| Build Speed | Same | Same |
| Cleanup | Easy (remove image) | Manual package removal |
| Debugging | Requires container access | Direct access |

## Next Steps

After building:
1. Test the executables: `./build/tes3mp --version`
2. Configure the client and server
3. Copy Morrowind data files
4. Run the server or join a game

## Additional Resources

- [TES3MP Wiki](https://github.com/TES3MP/TES3MP/wiki)
- [Docker Documentation](https://docs.docker.com/)
- [BUILD_INSTRUCTIONS.md](BUILD_INSTRUCTIONS.md) - Native build guide
- [QUICKSTART.md](QUICKSTART.md) - Quick start guide

---

**Note:** The Docker build produces the same binaries as a native build. Choose Docker if you want a clean, reproducible build environment without installing dependencies on your host system.
