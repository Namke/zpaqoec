## 0.1.4

- Fixed Windows compiler selection when `-Compiler` is explicitly supplied.
- Explicit compiler paths are resolved as literal files before PATH lookup.
- `-Compiler` is now authoritative: a rejected explicit compiler produces its own diagnostic and never falls back to Cygwin `g++`.
- Added common `C:\Programs\msys64\...` auto-detection paths.

# Changelog

## 0.1.2

- Fix Windows/MinGW build when patched `zpaqfranz.cpp` is compiled by absolute path: build scripts now pass the upstream source directory explicitly with `-I`, so `#include "extensions/zpaqfranz_ext.hpp"` resolves reliably.
- Normalize the upstream source path before compiling and verify that the injected extension header actually exists.
- Apply the same explicit include-root behavior to Linux builds for consistency.

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

## 0.1.3 - 2026-09-01

- Fixed Windows toolchain selection: `build-windows.ps1` now requires a Windows-targeting MinGW compiler instead of blindly invoking the first `g++` in `PATH`.
- Auto-detects upstream-recommended MSYS2 UCRT64 first (`C:\msys64\ucrt64\bin\g++.exe`), then other MinGW candidates.
- Added `-Compiler` and `ZPAQFRANZ_GXX` overrides.
- Prints compiler path and `-dumpmachine` target before compiling.
- Fails early with a focused diagnostic when `g++` is a Linux/Cygwin/non-MinGW target, preventing misleading POSIX API errors (`fseeko`, `ftello`, `fileno`, `ftruncate`, `usleep`, `realpath`, `select`, etc.).
- Windows detection in extension headers now also recognizes `__MINGW32__` / `__MINGW64__` defensively.
- Keeps the explicit upstream include root added in 0.1.2.
