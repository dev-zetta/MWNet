# TES3MP dependency manifest

TES3MP 1.0.0 uses the dependencies below in addition to the libraries inherited from OpenMW 0.52.

| Dependency | Version or revision | Acquisition | License | Purpose |
| --- | --- | --- | --- | --- |
| GameNetworkingSockets | `fa489fd2cb0fc86ef2503e330935d3eb03a6a064` (v1.5.1) | System CMake package, or pinned `FetchContent` when `TES3MP_FETCH_DEPS=ON` | BSD-3-Clause | Encrypted reliable and unreliable transport |
| libsodium | 1.0.18 or newer | Required system or toolchain package | ISC | Server identity, TOFU authentication, password hashing and secret-memory handling |

The FetchContent fallback is disabled by default. Release and CI builds must resolve the exact revision recorded above; changing it requires dependency review and an update to this manifest and the generated SBOM.

OpenMW's inherited dependencies remain documented by its build system, platform dependency manifests and third-party notices. The release SBOM supplements those sources; it does not replace license texts or attribution requirements.

TES3MP's additional GPL terms and the complete third-party notice set require specialist legal review before stable 1.0.0. This repository does not declare that review complete.
