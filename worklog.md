## 0.3.8 - OEC ignore filtering

- Added automatic `zpaq.ignore` filtering to `oec_a`.
- Added optional `.gitignore` integration via `-gitignore`.
- Git-style rules are resolved by OEC to final native exclusions before add.
- Progressive JSON MD5 respects the same filter set.
- Added dedicated ignore regression suite.

## 0.3.7 - Windows CRT directory enumeration

Fixed current MSYS2 UCRT64 compilation for JSON/MD5 filesystem walking. OEC now explicitly includes the Windows CRT I/O/stat headers and uses `_finddata_t` with `_findfirst/_findnext`, because the helpers only consume directory-entry names and attributes; actual file sizes remain 64-bit through `_stat64`/MD5 file I/O. Added a static regression for the exact incomplete `_finddata64_t` failure.

## 0.3.7 - progressive JSON MD5

Implemented force-extract MD5 catalogs and source-side progressive MD5 refresh on `oec_a`, with atomic JSON replacement and force-create gating.

## 0.3.4 - JSON catalog command

Implemented `oec_json` / `oec_j` as an immutable machine-readable current-file catalog. Output naming is derived from the archive identity and separators immediately before wildcard runs are trimmed. Existing JSON causes an immediate non-overwrite failure. The command consumes a valid mmap IDX list view when possible and otherwise uses one terse zero-part list pass. Per-file whole-file hashes remain null because standard list metadata does not expose one; no payload decompression is performed solely for hashing.

## 0.3.3 - PASSWORD_FOLDER

Implemented password-file preflight at the OEC bridge so it runs before upstream password queries. The resolver never places the plaintext password on the command line; it sets process-local `FRANZKEY`, inherited by native child passes. Regression covers single archive, wildcard naming, direct native metadata command, and FRANZKEY precedence.

## 0.3.2 - encrypted IDX completion/hang fix

- Traced post-`protected zero part` apparent hang to `build_idx_cache`: native `l` and `i` children were waiting for AES passwords while stdout/stderr were captured.
- Added staged IDX progress, visible password-wait notices, auth-option propagation, prompt chatter stripping, and encrypted-cache privacy policy.
- Added encrypted single-archive regression including interactive pty password handoff.

# Worklog

## 0.3.1 - single-part compatibility

- Added exact-file single archive detection, minimum EC-first init ordering, zero-index/IDX generation, OEC read routing, and single-file add coverage.
- Added fault injection proving `.ec` survives a failed native zero-index generation.

## 0.3.1 - OEC `.idx` cache milestone

Implemented a real disposable, mmap-backed cache layer without changing any ZPAQ archive part bytes.

### Cache lifecycle

- `OECIDX1` v1 header + CRC32C-protected sections.
- Source authority remains `.000`.
- Cache validity uses `.000` size/mtime plus first/middle/last 64 KiB CRC32C samples.
- Transactional `.tmp` -> final replacement; cache corruption/staleness never becomes archive corruption.
- Explicit `--idx PATH` allows SSD/NVMe placement.
- `oec_idx build|verify|info|drop`.

### Command integration

- `oecinit`: build/reuse `.idx` by default; `--no-idx` opt-out.
- `oec_l/i`: default forms use mmap cache directly and make zero native parser calls on a hit. Lazy rebuild on stale/missing/corrupt cache.
- `oec_l/i` with native options: fall back to `.000` parser for exact semantics.
- `oec_a`: cache lifecycle + optional `--idx-refresh`; standard native Jidac dedup remains authoritative.
- `oec_x/e`: cache validation wired, native multipart payload path retained.

### Deliberate boundary

The upstream add path still calls `read_archive()` into HT/DT and constructs native `HTIndex`; therefore this milestone does not claim that archive-scale dedup/file state has been moved out of RAM. Replacing those structures is a deeper source-level backend project and will be enabled only after binary-equivalence/dedup regression coverage is strong enough.

Likewise the v1 cache has no stable fragment->part/block locator section yet, so extraction does not bypass upstream multipart decoding.

### Regression

- injector PASS
- EC PASS
- OEC_A PASS
- OECINIT PASS
- OEC command routing PASS
- OEC IDX mmap/stale/corrupt/self-heal PASS
- Windows build script static tests PASS

## Next deep optimization

1. Add structured fragment/hash and current-file record sections to a future OECIDX format revision.
2. Patch upstream add hot path behind an OEC-only gate so native `HTIndex` can use SSD-backed lookup without changing normal zpaqfranz behavior.
3. Measure resident RAM and dedup equivalence on large archives before making disk-backed dedup the default.
4. Add fragment->part/block locators and use them in `oec_x/e` only after extraction equivalence tests pass.
5. Stream EC directly from ordered ZPAQ output buffers to remove the post-close reread for newly created parts.

## 0.4.1
- Added centralized default IDX relocation through `EOC_TEMP`.
- Preserved explicit `--idx` precedence and legacy archive-local defaults when env is absent.
- Added IDX regression cases for env/default/explicit path resolution.
