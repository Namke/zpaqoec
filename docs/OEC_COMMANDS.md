# zpaqoec OEC command guide — 0.5.0

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
zpaqoec OEC overlay 0.5.0 (Optimize + Error Correction)
```

## `oecinit` / `oec_init`

Retrofit an existing single-file or multipart archive:

```bash
zpaqoec oecinit "compress.???"
zpaqoec oec_init "compress.???"
```

For multipart input it creates/reuses `.000`, generates missing `.ec` files, seeds `.ecstate`, and by default builds/reuses the mmap `.idx` cache.

For an exact existing single archive:

```bash
zpaqoec oec_init E:/LTS/archive.zpaq
```

the layout is:

```text
archive.zpaq          original payload
archive.zpaq.ec       payload EC (created first)
archive.000.zpaq      metadata-only zero part
archive.000.zpaq.ec   zero-part EC
archive.idx           mmap cache (unencrypted, or explicit plaintext opt-in)
```

The original `archive.zpaq` is not renamed or rewritten by `oec_init`. If zero-part generation fails, OEC returns a partial failure but preserves `archive.zpaq.ec` as the minimum protection layer. Single-file mode does not use `.ecstate`.

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

For standard AES-encrypted `.000`, OEC does **not** create a plaintext `.idx` by default. This avoids leaking filenames/metadata from an encrypted archive into a disposable cache. To opt in deliberately:

```bash
zpaqoec oecinit "secret.???" -key PASSWORD --idx X:/FastCache/secret.idx --idx-plaintext
```

Or with zpaqfranz 64.8's password environment variable:

```powershell
$env:FRANZKEY='PASSWORD'
zpaqoec.exe oec_init secret.zpaq --idx X:\FastCache\secret.idx --idx-plaintext
Remove-Item Env:FRANZKEY
```

If `--idx-plaintext` is selected without a reusable key, IDX materialization runs native `l` and `i` passes. OEC shows `stage 1/2` and `stage 2/2` and explicitly tells you when a password is required.

EC options:

```text
--ec-data N
--ec-shard BYTES
--ec-stripes N
--no-index-ec
--no-part-ec
```

## `PASSWORD_FOLDER` automatic password files

Set `PASSWORD_FOLDER` to a directory of plaintext one-line password files. Before an OEC/native archive command reaches upstream password input, zpaqoec derives a password filename from the archive name.

```text
test???.zpaq  -> test.password
nen.zpaq      -> nen.password
```

It also normalizes OEC zero parts such as `nen.000.zpaq -> nen.password`. The password is loaded into process-local `FRANZKEY`, so child `x -index`, `l`, and `i` passes reuse it automatically without putting it on the command line.

```powershell
$env:PASSWORD_FOLDER='C:\Keys\Zpaq'
zpaqoec.exe oec_init 'E:\archives\test???.zpaq'
```

Resolution order: explicit `-key`/`-franzen`, existing `FRANZKEY`, matching password file, then upstream interactive prompt. Only the first line is used; CR/LF and an optional UTF-8 BOM are stripped. Missing/unreadable/empty files preserve the normal interactive fallback.

## `oec_idx`

Build cache explicitly:

```bash
zpaqoec oec_idx ensure compress --idx X:/FastCache/compress.idx
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

For encrypted metadata, add `--idx-plaintext` plus `-key PASSWORD` (or set `FRANZKEY`) if you intentionally want a plaintext cache.

or override authority explicitly:

```bash
zpaqoec oec_idx build backup \
  --oec-index X:/metadata/backup.000 \
  --idx X:/FastCache/backup.idx
```

See [`OEC_IDX_FORMAT.md`](OEC_IDX_FORMAT.md) for cache layout and validity rules.


IDX2 management actions:

```bash
zpaqoec oec_idx verify  compress --idx X:/FastCache/compress.idx
zpaqoec oec_idx ensure  compress --idx X:/FastCache/compress.idx
zpaqoec oec_idx upgrade compress --idx X:/FastCache/compress.idx
zpaqoec oec_idx rebuild compress --idx X:/FastCache/compress.idx
```

`ensure` verifies and self-heals missing/stale/corrupt/old caches. `upgrade` migrates valid IDX1 to IDX2 by rebuilding from `.000`. New IDX2 contains structured FILE_TABLE/STRING_POOL/PATH_HASH sections in addition to LIST/INFO compatibility views.


### `EOC_TEMP` default IDX directory

Set `EOC_TEMP` to place all automatically derived IDX files in a fast cache directory. The existing archive-derived IDX basename is preserved. `--idx PATH` overrides `EOC_TEMP`; if the environment variable is unset or empty, IDX remains beside the archive as before.

```powershell
$env:EOC_TEMP = 'X:\OEC-Cache'
zpaqoec oec_idx ensure E:\Archives\compress
zpaqoec oec_l E:\Archives\compress
```

## `oec_a`

```bash
zpaqoec oec_a compress /data -method 5
```

Single-file archive:

```bash
zpaqoec oec_a archive.zpaq /data -method 5 --idx X:/FastCache/archive.idx
```

In single mode the same `archive.zpaq` is appended, `archive.000.zpaq` remains the external metadata index, and `archive.zpaq.ec` is regenerated after a successful add.

Without `-chunk`, OEC keeps the existing native external-index path:

```text
a "compress.???" /data -method 5 -index compress.000
```

With native zpaqfranz `-chunk`, upstream 64.8 rejects `-chunk` together with `-index`. OEC therefore preserves the exact native archive-writing command and rebuilds the metadata-only `.000` only after the add commits:

```text
a "compress.???" /data -method 5 -chunk 4g
# data parts are now committed exactly as upstream wrote them
x "compress.???" -index compress.000.oec-rebuild.tmp -force
# atomic install -> compress.000
```

A single chunked add may create several physical parts. OEC discovers the complete newly-created contiguous range, protects **every** new part with its own `.ec`, updates `.ecstate` to the highest committed part, and never reopens the final short part from the previous add. For example, if one add creates parts `001..004`, the next add begins at `005`; part `004` remains immutable.

### Recursive ignore filtering

`oec_a` automatically checks every source folder for `zpaq.ignore`. The file uses Git-style patterns and applies recursively below that source root:

```gitignore
# skip temporary files everywhere
*.tmp

# skip a whole directory tree
build/
cache/**

# re-include one file
!important.tmp

# root-relative rule
/secrets.local
```

Run normally; no switch is needed for `zpaq.ignore`:

```bash
zpaqoec oec_a backup /data -method 5
```

To also use `/data/.gitignore`:

```bash
zpaqoec oec_a backup /data -method 5 -gitignore
```

Supported rule features include `*`, `?`, `**`, `[abc]`/ranges, comments beginning with `#`, escaped leading `\#`/`\!`, trailing `/` directory rules, leading `/` root anchoring, and `!` negation. Rules are evaluated in file order. When both files are enabled, `.gitignore` is loaded first and `zpaq.ignore` second, so OEC-specific rules can override Git rules.

OEC resolves the final ignored file/directory set before the native add and passes a temporary `-exclude` file to zpaqfranz. The temporary file is removed immediately after add. Progressive JSON/MD5 source hashing uses the same filtered set.

Only the ignore file in each explicitly supplied source folder is loaded; patterns from that file filter descendants recursively. OEC does not automatically discover nested `.gitignore`/`zpaq.ignore` files in subdirectories in 0.5.0.

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

JSON maintenance:

```bash
# Existing compress.json is updated automatically with source-side MD5.
zpaqoec oec_a compress /data -method 5

# Create it if missing:
zpaqoec oec_a compress /data -method 5 --json-force
```

**0.5.0 boundary:** `oec_a` replaces the transient upstream `HTIndex` lookup allocation with hybrid RAM+mmap IDX2 dedup when possible. `Jidac::HT` and `Jidac::DT` remain in RAM, so RAM is reduced but not constant. Use `--idx-memory auto|0|SIZE`; failures automatically fall back to upstream `HTIndex`.

## `oec_json` / `oec_j`

Write the current archive file set as UTF-8 JSON beside the archive:

```bash
zpaqoec oec_json "aaa???.zpaq"   # -> aaa.json
zpaqoec oec_j bbb.zpaq            # -> bbb.json
```

The output name is derived from archive identity. Wildcard runs and the separator immediately before them are removed (`backup_????????.zpaq -> backup.json`). A direct `oec_json` call is **non-overwriting**: if the JSON already exists the command exits immediately before archive metadata or payload is read.

### Whole-file MD5

Standard ZPAQ catalog metadata does not expose a stored whole-file MD5. To populate it explicitly:

```bash
zpaqoec oec_json bbb.zpaq --force-md5
```

OEC performs one native extraction into an isolated temporary tree, calculates MD5 for every current file, then removes the tree. Each hashed record contains:

```json
{
  "path": "folder/file.bin",
  "size": 1234,
  "md5": "0123456789abcdef0123456789abcdef",
  "md5_source": "archive-extract",
  "hash": {"algorithm":"MD5","value":"0123456789abcdef0123456789abcdef"}
}
```

The root schema is format version 2 and includes `md5_file_count` and `md5_complete`. Without `--force-md5`, `md5` is null unless it was populated by a progressive `oec_a` update.

### Progressive maintenance by `oec_a`

After every successful `oec_a`:

- if the archive JSON already exists, OEC refreshes it automatically;
- files supplied by this add are hashed directly from the source filesystem (no archive extraction);
- MD5 from the previous JSON is retained only when path, size, and modification timestamp are unchanged;
- deleted files disappear because the new JSON is rebuilt from the current `.000` catalog;
- JSON is written to a temporary file and atomically installed only after the archive add/EC work succeeds.

If no JSON exists, normal `oec_a` does not create one. Opt in with:

```bash
zpaqoec oec_a compress /data --json-force
# alias: --force-json
```

Supported `oec_json` options:

```text
--force-md5
--digits N
--oec-index PATH
--idx PATH
--no-idx
--idx-plaintext
-key PASSWORD / -franzen PASSWORD
```

When a valid `.idx` exists and its list view is parseable, `oec_json` reads it through mmap. Otherwise it performs one native terse list pass against `.000`. Starting in 0.5.0, if that terse layout is not parseable, OEC retries `l -all -terse -nocolor` and collapses the explicit version/status history to the current live set. The parser also accepts pipe-status and plain/legacy layouts, compact/slash timestamps, and strips ANSI escape sequences. `PASSWORD_FOLDER` and `FRANZKEY` work as with the other OEC commands, including the extraction pass used by `--force-md5`.

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

The cache is validated as OEC metadata acceleration state. Actual payload still comes from multipart data through the native extractor because `.000` contains no D blocks. 0.5.0 does not yet claim direct fragment-to-part seeking.

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

## EC / archive integrity operations

Per-file EC commands remain available:

```bash
zpaqoec ec create compress.001
zpaqoec ec verify compress.001
zpaqoec ec repair compress.001 --output compress.001.repaired
zpaqoec ec info compress.001.ec
```

Whole-archive read-only verification checks every data part, the `.000` sidecar, and the disposable IDX cache:

```bash
zpaqoec oec_check compress
# alias
zpaqoec oec_verify compress
```

For wildcard naming:

```bash
zpaqoec oec_check "backup_????????.zpaq"
```

`oec_check` returns `0` when clean, `2` when maintenance is needed but EC reports the damage as repairable (including missing/stale IDX or missing EC), and `3` for structural/unrecoverable integrity problems. It does not modify anything.

Self-heal the archive maintenance layer:

```bash
zpaqoec oec_fix compress
```

`oec_fix` performs these operations in order:

- rebuild a missing authoritative `.000` from the native multipart archive;
- create a missing EC sidecar;
- repair EC-recoverable damaged data and preserve the damaged original as `*.oec-bad[.N]`;
- regenerate damaged parity when the data shards verify cleanly;
- verify/protect `.000`;
- rebuild a missing/stale/corrupt disposable `.idx` from `.000`.

A structurally unreadable EC sidecar is not silently trusted. If the data file is independently known-good and you intentionally want to replace such a sidecar, use:

```bash
zpaqoec oec_fix compress --rebuild-ec
```

IDX-only maintenance is still available and is the quickest command if only the SSD cache was deleted:

```bash
zpaqoec oec_idx ensure compress --idx X:/FastCache/compress.idx
```

Deleting `.idx` is always safe: `.000` is authoritative and `ensure`, `oec_fix`, or the normal lazy metadata path can recreate it.

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
