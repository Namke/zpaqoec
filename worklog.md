# Worklog

## 0.3.0 - OEC `.idx` cache milestone

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
