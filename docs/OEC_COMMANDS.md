# zpaqoec OEC command guide — 0.3.0

OEC means **Optimize + Error Correction**. Original zpaqfranz commands remain available unchanged; use the `oec_*` namespace for the fork's optimized/error-corrected workflow.

## Layout

```text
compress.000          authoritative standard ZPAQ metadata-only index
compress.000.ec       EC for zero part
compress.001          standard ZPAQ data part
compress.001.ec       independent EC sidecar
compress.002
compress.002.ec
...
compress.ecstate      next-part checkpoint

# optional and disposable
X:/FastCache/compress.idx
```

`.idx` may be placed on SSD/NVMe. Losing it does not lose the archive.

## Quick help / identity

```bash
zpaqoec
zpaqoec oec_h
zpaqoec oec_help
zpaqoec oec_version
```

Expected identity:

```text
zpaqoec OEC overlay 0.3.0 (Optimize + Error Correction)
```

## `oecinit` / `oec_init`

Retrofit an existing multipart archive:

```bash
zpaqoec oecinit "compress.???"
zpaqoec oec_init "compress.???"
```

It creates/reuses `.000`, generates missing `.ec` files, seeds `.ecstate`, and by default builds/reuses the mmap `.idx` cache.

Put cache on SSD:

```bash
zpaqoec oecinit "compress.???" --idx X:/FastCache/compress.idx
```

Disable cache:

```bash
zpaqoec oecinit "compress.???" --no-idx
```

Force zero-part/EC/cache regeneration:

```bash
zpaqoec oecinit "compress.???" --force --idx X:/FastCache/compress.idx
```

Generic naming:

```bash
zpaqoec oecinit "backup_????????.zpaq"
```

Custom zero part:

```bash
zpaqoec oecinit "backup_????????.zpaq" -index X:/metadata/backup_00000000.zpaq
```

Encrypted archive read options pass to the native index builder:

```bash
zpaqoec oecinit "secret.???" -key PASSWORD
```

EC options:

```text
--ec-data N
--ec-shard BYTES
--ec-stripes N
--no-index-ec
--no-part-ec
```

## `oec_idx`

Build cache explicitly:

```bash
zpaqoec oec_idx build compress --idx X:/FastCache/compress.idx
```

Verify:

```bash
zpaqoec oec_idx verify compress --idx X:/FastCache/compress.idx
```

Info:

```bash
zpaqoec oec_idx info compress --idx X:/FastCache/compress.idx
```

Drop:

```bash
zpaqoec oec_idx drop compress --idx X:/FastCache/compress.idx
```

For nonstandard layouts:

```bash
zpaqoec oec_idx build "backup_????????.zpaq" \
  --idx X:/FastCache/backup.idx
```

or override authority explicitly:

```bash
zpaqoec oec_idx build backup \
  --oec-index X:/metadata/backup.000 \
  --idx X:/FastCache/backup.idx
```

See [`OEC_IDX_FORMAT.md`](OEC_IDX_FORMAT.md) for cache layout and validity rules.

## `oec_a`

```bash
zpaqoec oec_a compress /data -method 5
```

OEC owns the native `-index` path and conceptually delegates:

```text
a "compress.???" /data -method 5 -index compress.000
```

New part EC and zero-part EC are then created/refreshed.

Cache path:

```bash
zpaqoec oec_a compress /data \
  --idx X:/FastCache/compress.idx
```

Default behavior does not immediately rebuild the cache after every add. The changed `.000` fingerprint makes the old cache stale, and the next default `oec_l/oec_i` rebuilds it lazily.

Refresh during add:

```bash
zpaqoec oec_a compress /data \
  --idx X:/FastCache/compress.idx --idx-refresh
```

Disable cache lifecycle:

```bash
zpaqoec oec_a compress /data --no-idx
```

**0.3.0 boundary:** native zpaqfranz still reconstructs Jidac/fragment/file state for dedup. `.idx` does not yet replace that RAM state.

## `oec_l`

Default optimized/mmap path:

```bash
zpaqoec oec_l compress
zpaqoec oec_l compress --idx X:/FastCache/compress.idx
```

A valid cache hit does not invoke the native `.000` parser.

Missing/stale/corrupt cache is rebuilt lazily. Disable rebuild:

```bash
zpaqoec oec_l compress \
  --idx X:/FastCache/compress.idx --idx-no-rebuild
```

Bypass cache:

```bash
zpaqoec oec_l compress --no-idx
```

Option-rich native list semantics fall back to `.000` parsing:

```bash
zpaqoec oec_l compress -all --idx X:/FastCache/compress.idx
```

## `oec_i`

```bash
zpaqoec oec_i compress
zpaqoec oec_i compress --idx X:/FastCache/compress.idx
```

Default form uses the same mmap/lazy-rebuild behavior as `oec_l`.

## `oec_x`

```bash
zpaqoec oec_x compress path/to/file -to restore
```

Cache selection is accepted:

```bash
zpaqoec oec_x compress path/to/file -to restore \
  --idx X:/FastCache/compress.idx
```

The cache is validated as OEC metadata acceleration state. Actual payload still comes from multipart data through the native extractor because `.000` contains no D blocks. 0.3.0 does not yet claim direct fragment-to-part seeking.

## `oec_e`

```bash
zpaqoec oec_e compress path/to/file
```

Current payload/cache boundary is the same as `oec_x`.

## Common OEC read routing options

```text
--digits N
--oec-index PATH
--idx PATH
--no-idx
--idx-no-rebuild
```

Do not pass native `-index` to `oec_l/i/x/e`; use `--oec-index` for the OEC zero-part authority.

## EC operations

```bash
zpaqoec ec create compress.001
zpaqoec ec verify compress.001
zpaqoec ec repair compress.001 --output compress.001.repaired
zpaqoec ec info compress.001.ec
```

## Windows examples

```powershell
.\build\zpaqoec.exe oec_init `
  'W:\LTS\ZPacks\Programs\Games\202601\202601????.zpaq' `
  --idx 'X:\ZpaqCache\202601.idx'

.\build\zpaqoec.exe oec_l `
  'W:\LTS\ZPacks\Programs\Games\202601\202601????.zpaq' `
  --idx 'X:\ZpaqCache\202601.idx'
```

Build:

```powershell
.\scripts\build-windows.ps1 `
  -Compiler 'C:\Programs\msys64\ucrt64\bin\g++.exe'
```

## Linux examples

```bash
./scripts/build-linux.sh /src/zpaqfranz.cpp
./build/zpaqoec oecinit '/archive/compress.???' --idx /nvme/oec/compress.idx
./build/zpaqoec oec_l /archive/compress --idx /nvme/oec/compress.idx
```

## Native baseline remains available

```bash
zpaqoec a ...
zpaqoec l ...
zpaqoec i ...
zpaqoec x ...
zpaqoec e ...
```

These retain upstream behavior and are useful as a compatibility/regression baseline.
