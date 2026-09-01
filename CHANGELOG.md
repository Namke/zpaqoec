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
