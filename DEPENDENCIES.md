# TES3MP dependency manifest

TES3MP 1.0.0 uses the dependencies below in addition to the libraries inherited from OpenMW 0.52.

| Dependency | Version or revision | Acquisition | License | Purpose |
| --- | --- | --- | --- | --- |
| GameNetworkingSockets | `fa489fd2cb0fc86ef2503e330935d3eb03a6a064` (v1.5.1) | System CMake package, or pinned `FetchContent` when `TES3MP_FETCH_DEPS=ON` | BSD-3-Clause | Encrypted reliable and unreliable transport |
| libsodium | 1.0.18 or newer | Required system or toolchain package | ISC | Server identity, TOFU authentication, password hashing and secret-memory handling |

The FetchContent fallback is disabled by default. Release and CI builds must resolve the exact revision recorded above; changing it requires dependency review and an update to this manifest and the generated SBOM.
