# Security policy

TES3MP 1.0.0 is undergoing a security-focused protocol and authority rewrite. The current alpha is for local and explicitly opt-in testing; it is not approved for untrusted public hosting.

## Supported versions

| Version | Security support | Network exposure |
| --- | --- | --- |
| 1.0.0 alpha | Active development | Loopback by default; private testing only |
| 0.8.1 and protocol 10 | Unsupported | Do not expose to untrusted peers |
| Older releases | Unsupported | Do not expose |

Stable 1.0.0 remains blocked until the cross-platform, sanitizer, fuzzing, soak, migration, independent security-review and legal-review gates in the release plan pass.

## Reporting a vulnerability

Do not open a public issue for an unpatched vulnerability. Email `dev@zetta.app` with the subject `TES3MP security report`, or use the repository host's private security-reporting feature when one is available.

Include the affected commit or version, operating system, whether the client or server is affected, reproduction steps and the security impact. Attach only the smallest necessary logs and remove passwords, identity private keys, access tokens, player data and unrelated IP addresses. A proof of concept is welcome, but do not test against public servers or data you do not own.

The maintainer will acknowledge a report as soon as practical, coordinate a fix and disclosure date, and credit the reporter unless anonymity is requested. No response-time or bounty guarantee is made.

## Operational guidance

- Keep alpha servers bound to loopback unless every participant is trusted and public listening was explicitly enabled.
- Verify a server fingerprint over an independent channel before accepting it. Treat any later mismatch as a possible interception or server-key replacement; never bypass it silently.
- Protect the server identity key, account store and world/player data with operating-system permissions and backups. Never publish them with logs or bug reports.
- Use a dedicated unprivileged account or container, restrict filesystem access to game data, and expose only the selected game port.
- Keep clients, servers, CoreScripts, content files and load order identical.
- Do not load unreviewed server scripts. Server-side Lua is trusted operator code and can access gameplay state and the configured data directory.

See [THREAT_MODEL.md](THREAT_MODEL.md) for trust boundaries, implemented controls and known residual risks.
