# 0.1.1

- Added `trunkinit` to retrofit existing multipart ZPAQ archives.
- Rebuilds the external zero-part index through native `x ... -index ...` semantics.
- Generates independent EC sidecars for every existing data part and for the rebuilt trunk.
- Uses a temporary index and safe replace so a failed rebuild does not modify archive data parts.
- Supports conventional `BASE.???` and generic contiguous `?` patterns such as `backup_????????.zpaq`.
- Seeds `BASE.ecstate` automatically for the conventional `BASE.???` layout.
- Added regression coverage for retrofit, force rebuild, generic `.zpaq` naming, EC verification, and existing trunkadd flow.

# Changelog

## 0.1.0 - 2026-09-01

- Added ZFEC v1 independent sidecar format.
- Added CRC32C detection for data/parity/metadata.
- Added GF(256) P+Q recovery for up to 2 bad data shards per stripe.
- Added bounded-memory window interleaving for burst bitrot resistance.
- Added `ec create`, `ec verify`, `ec repair`, `ec info`.
- Added `trunkadd BASE ...` orchestration over upstream `-index` multipart add.
- Added transactional `.ecstate` part checkpoint to avoid normal filename walks.
- Added automatic `.ec` generation for new archive part and trunk.
- Added source injector for zpaqfranz monolith.
- Added Linux and Windows/MSYS2 build scripts.
- Added corruption, truncation, double-loss and sanitizer tests.
