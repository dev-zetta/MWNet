# CrabNet Naming Convention

This document clarifies the naming conventions used for CrabNet (TES3MP's RakNet fork) in this project.

## Directory Structure

✅ **Updated to use "crabnet":**
- Directory: `dependencies/crabnet/` (renamed from `dependencies/raknet/`)
- Build script variable: `CRABNET_LOCATION`

## Documentation References

✅ **Updated to use "CrabNet" in user-facing text:**
- "Build CrabNet" (not "Build RakNet")
- "Missing CrabNet" (not "Missing RakNet")
- "CrabNet networking integration" (not "RakNet networking integration")
- "Skip CrabNet build" (not "Skip RakNet build")

✅ **Contextual references (correct as-is):**
- "CrabNet (TES3MP's RakNet fork)" - explains what CrabNet is
- "CrabNet (RakNet) Installation" - provides context
- "CrabNet is a fork of RakNet" - historical context

## Technical Names (Must NOT Change)

⚠️ **These MUST remain as "RakNet" - they are part of CrabNet's internal build system:**

### CMake Variables
```cmake
-DRakNet_INCLUDES=${CRABNET_LOCATION}/include
-DRakNet_LIBRARY_DEBUG=${CRABNET_LOCATION}/build/lib/libRakNetLibStatic.a
-DRakNet_LIBRARY_RELEASE=${CRABNET_LOCATION}/build/lib/libRakNetLibStatic.a
```

### Library Filename
- `libRakNetLibStatic.a` - This is the actual library name produced by CrabNet's build

### Include Directories
- `include/RakNet/` - CrabNet's header files are in this directory
- `include/raknet/` - Case-insensitive symlink for compatibility

### CMake Options
- `CRABNET_ENABLE_DLL` - CrabNet's CMake option (not RakNet)
- `CRABNET_ENABLE_STATIC` - CrabNet's CMake option (not RakNet)
- `CRABNET_ENABLE_SAMPLES` - CrabNet's CMake option (not RakNet)

## Why This Matters

**CrabNet** is TES3MP's maintained fork of the original **RakNet** library. While the project is called CrabNet:

1. **Internally**, CrabNet still uses "RakNet" for:
   - CMake variable names (for compatibility)
   - Library output names
   - Include directory structure
   - Namespace in C++ code

2. **Externally**, we use "CrabNet" for:
   - Directory names in our project
   - User-facing documentation
   - Build script messages
   - Variable names in our scripts

## Summary

- **User-facing**: Use "CrabNet"
- **Technical/Build**: Use "RakNet" (as required by CrabNet's build system)
- **Context**: "CrabNet (RakNet fork)" is correct and helpful

This naming convention ensures compatibility with CrabNet's build system while making it clear to users that we're using the CrabNet fork, not the original RakNet.
