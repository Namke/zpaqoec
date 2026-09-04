# zpaqoec 0.5.0

**OEC = Optimize + Error Correction.**

`zpaqoec` is a thin fork/overlay for zpaqfranz 64.8. The design rule is simple:

- keep existing single `.zpaq` payloads and `.001`, `.002`, ... multipart payloads as normal ZPAQ bytes;
- keep `.000` as the authoritative/portable ZPAQ metadata index;
- put repair redundancy in independent `.ec` sidecars;
- put disposable performance state in an optional `.idx` cache that can live on SSD/NVMe.

Original zpaqfranz commands (`a`, `l`, `i`, `x`, `e`, ...) remain available unchanged.

## Archive layout

```text
/archive/compress.000          standard ZPAQ metadata-only zero part
/archive/compress.000.ec       EC sidecar for the zero part
/archive/compress.001          normal ZPAQ data part
/archive/compress.001.ec       independent EC sidecar
/archive/compress.002
/archive/compress.002.ec
...
/archive/compress.ecstate      tiny next-part checkpoint

# optional, disposable; may live on another device
/fast-cache/compress.idx       OEC mmap acceleration cache
```

`.idx` is **not required for recovery**. Delete it and OEC can rebuild it from `.000`.

### Default IDX placement with `EOC_TEMP`

If `EOC_TEMP` is non-empty, OEC relocates the **default** `.idx` path into that directory while preserving the archive-derived cache basename. Explicit `--idx PATH` always has higher precedence. If `EOC_TEMP` is unset/empty, the historical default beside the archive is unchanged.

```powershell
$env:EOC_TEMP = 'X:\OEC-Cache'
zpaqoec oec_idx ensure E:\Archives\compress
# cache: X:\OEC-Cache\compress.idx

zpaqoec oec_l E:\Archives\compress
# automatically uses X:\OEC-Cache\compress.idx

# Explicit path wins over EOC_TEMP:
zpaqoec oec_idx ensure E:\Archives\compress --idx Y:\Pinned\compress.idx
```

## OEC commands

Run `zpaqoec` with no parameters for quick help. Full OEC usage is in [`docs/OEC_COMMANDS.md`](docs/OEC_COMMANDS.md).

| Command | Role | `.idx` behavior in 0.5.0 |
|---|---|---|
| `oecinit` / `oec_init` | retrofit existing single or multipart archive with `.000` + EC | builds/reuses `.idx` for unencrypted metadata; encrypted `.000` skips plaintext IDX unless `--idx-plaintext` |
| `oec_a` | incremental add + EC | preserves native `-chunk` writer semantics; chunked add rebuilds `.000` post-commit and protects every new part |
| `oec_l` | optimized native `l` equivalent | default form served directly from mmap cache; lazy rebuild from `.000` |
| `oec_i` | optimized native `i` equivalent | default form served directly from mmap cache; lazy rebuild from `.000` |
| `oec_x` | OEC native `x` equivalent | validates available cache; payload still delegated to multipart native extractor |
| `oec_e` | OEC native `e` equivalent | same current payload routing as `oec_x` |
| `oec_idx` | manage disposable mmap cache | `build`, `verify`, `ensure`, `upgrade`, `rebuild`, `info`, `drop` |
| `oec_json` / `oec_j` | machine-readable current-file catalog; direct creation is non-overwriting | mmap/`.000` metadata; `--force-md5` extracts once and hashes payload |
| `ec` | EC sidecar operations | `create`, `verify`, `repair`, `info` |

## JSON file catalog

```bash
zpaqoec oec_json "aaa???.zpaq"
# writes aaa.json beside the archive

zpaqoec oec_json bbb.zpaq
# writes bbb.json

# Full whole-file MD5 catalog (one extraction pass):
zpaqoec oec_json bbb.zpaq --force-md5
```

`oec_j` is an alias. Direct `oec_json` creation keeps the original non-overwrite rule: if the JSON already exists, OEC exits before parsing the archive. Each current record contains path, byte size, modification time, saved attributes, type, version/status, compression ratio, `md5`, `md5_source`, and a normalized `hash` object when MD5 is available.

By default no payload is read merely to manufacture a whole-file hash, so `md5`/`hash` may be null. Use `--force-md5` to extract the archive once to an isolated temporary tree, calculate MD5 for every current file, remove the temporary tree, and create a complete JSON catalog.

`oec_a` also maintains this JSON progressively. If `<archive>.json` already exists, each successful add rebuilds the current metadata view, keeps MD5 for unchanged files, calculates MD5 directly from source files participating in the new add, and atomically replaces the JSON. If no JSON exists, normal `oec_a` leaves it absent; `--json-force` (alias `--force-json`) opts in to creating it.

A valid `.idx` list view is consumed via mmap when possible; otherwise OEC performs one `l <zero-part> -terse -nocolor` pass.

### Current acceleration boundary

0.5.0 upgrades the mmap cache to **OECIDX2**: structured FILE_TABLE + STRING_POOL + sorted PATH_HASH sections are stored alongside LIST/INFO compatibility views. `oec_l`/`oec_i` still use cached native views for exact presentation, while `oec_json` consumes structured file records directly without reparsing list text.

Option-rich forms such as `oec_l compress -all` still call the native `.000` parser so upstream filtering/version semantics remain exact.

0.5.0 adds the first deep IDX backend for `oec_a`: the large transient upstream `HTIndex` hash lookup is replaced by `OecHybridHTIndex`, using a bounded RAM hot cache over an mmap IDX2 `FRAGMENT_TABLE`. The authoritative `Jidac::HT` and `Jidac::DT` vectors are still reconstructed in RAM, so this milestone reduces the dedup lookup RAM peak but does **not** claim constant-memory add yet. If the deep table cannot be opened/validated, the adapter constructs the original upstream `HTIndex` as a correctness fallback.

Likewise `oec_x/oec_e` still let upstream decode multipart payload. The cache is validated and available, but fragment-to-part direct seeking is not claimed yet.

## Initialize / retrofit

```bash
zpaqoec oecinit "compress.???"
```

Equivalent alias:

```bash
zpaqoec oec_init "compress.???"
```

This creates/reuses:

```text
compress.000
compress.000.ec
compress.001.ec
compress.002.ec
...
compress.ecstate
compress.idx
```

Existing `.001...NNN` parts are never rewritten.

### Legacy single-file archive

An exact existing archive path is treated as a single-part archive, not as a multipart base:

```bash
zpaqoec oec_init E:/archive/backup.zpaq
```

OEC keeps the original data file and creates:

```text
backup.zpaq          original standard ZPAQ payload, unchanged by init
backup.zpaq.ec       minimum EC protection for the payload
backup.000.zpaq      metadata-only OEC zero part
backup.000.zpaq.ec   EC for the zero part
backup.idx           disposable mmap cache (unencrypted archive, or explicit `--idx-plaintext`)
```

Single-file init writes `backup.zpaq.ec` **before** attempting zero-part generation. If native index generation fails, the command returns an error but the payload EC remains usable. A single archive does not need `.ecstate`; `oec_a backup.zpaq ...` appends to the same archive, updates `backup.000.zpaq`, and regenerates the payload EC.

Place the disposable cache on a fast SSD/NVMe:

```bash
zpaqoec oecinit "compress.???" --idx X:/ZpaqCache/compress.idx
```

Disable cache creation:

```bash
zpaqoec oecinit "compress.???" --no-idx
```

## Password folder lookup

`PASSWORD_FOLDER` can point to a directory containing one-line plaintext password files. If an archive command has no explicit `-key`/`-franzen` and `FRANZKEY` is not already set, zpaqoec checks this folder before upstream asks interactively.

```powershell
$env:PASSWORD_FOLDER = 'X:\ArchivePasswords'
zpaqoec.exe oec_init 'D:\Backup\test???.zpaq'
```

Examples of file-name mapping:

```text
test???.zpaq       -> X:\ArchivePasswords\test.password
compress.???       -> X:\ArchivePasswords\compress.password
nen.zpaq           -> X:\ArchivePasswords\nen.password
nen.000.zpaq       -> X:\ArchivePasswords\nen.password
```

The first line is used verbatim except for UTF-8 BOM and trailing CR/LF removal. On a successful match the password is placed only in process-local `FRANZKEY` and inherited by child zpaqfranz passes; it is not appended to the command line or printed. If the file is missing, unreadable, or empty, normal interactive password handling remains unchanged.

Precedence is: explicit `-key`/`-franzen` > existing `FRANZKEY` > `PASSWORD_FOLDER` > interactive prompt.

### AES-encrypted archives and IDX

The `.idx` payload contains plaintext filename/metadata sections (IDX2 adds structured records in addition to LIST/INFO). Therefore OEC 0.5.0 does **not** create or automatically use a plaintext `.idx` when the authoritative `.000` is standard AES-encrypted. `oecinit` still completes after producing the payload EC, encrypted `.000`, and `.000.ec`.

To explicitly allow a plaintext SSD cache:

```bash
zpaqoec oecinit secret.zpaq --idx X:/FastCache/secret.idx --idx-plaintext
```

For non-interactive operation, pass the normal zpaqfranz authentication option or use the upstream environment variable:

```powershell
$env:FRANZKEY='password'
zpaqoec.exe oec_init secret.zpaq --idx X:\FastCache\secret.idx --idx-plaintext
Remove-Item Env:FRANZKEY
```

Without `FRANZKEY`/`-key`, an explicit plaintext IDX build may require additional native `l` and `i` authentication passes. OEC prints `stage 1/2` and `stage 2/2` plus a visible waiting-for-password message; 0.3.1 incorrectly captured those prompts and appeared to hang.

If `.000` and `.idx` already exist and the cache fingerprint is valid, `oecinit` reuses both. `--force` rebuilds the zero part/EC/cache.

## `.idx` manager

Build explicitly:

```bash
zpaqoec oec_idx build compress --idx X:/ZpaqCache/compress.idx
```

Verify source fingerprint + header + section CRC32C:

```bash
zpaqoec oec_idx verify compress --idx X:/ZpaqCache/compress.idx
```

Inspect:

```bash
zpaqoec oec_idx info compress --idx X:/ZpaqCache/compress.idx
```

Drop the disposable cache:

```bash
zpaqoec oec_idx drop compress --idx X:/ZpaqCache/compress.idx
```

The format is documented in [`docs/OEC_IDX_FORMAT.md`](docs/OEC_IDX_FORMAT.md).

## Incremental add

```bash
zpaqoec oec_a compress /data -method 5
```

For a normal non-chunk add, the existing external-index path remains native ZPAQ:

```text
a "compress.???" /data -method 5 -index compress.000
```

For native chunked multipart, OEC deliberately does **not** combine `-chunk` with `-index` because zpaqfranz 64.8 rejects that combination. The archive-writing call is exactly the upstream form:

```text
a "compress.???" /data -method 5 -chunk 4g
```

After commit, OEC rebuilds `.000` in a separate native `x -index` pass, then creates EC for every new physical part and for `.000`. If an add creates `001..004`, the next add starts at `005`; `004` is never reopened or filled.

By default an existing `.idx` becomes stale when `.000` changes. The next default `oec_l/oec_i` validates the fingerprint and lazily rebuilds it. To pay the refresh cost during add instead:

```bash
zpaqoec oec_a compress /data -method 5 \
  --idx X:/ZpaqCache/compress.idx --idx-refresh
```

Disable cache lifecycle for the add:

```bash
zpaqoec oec_a compress /data --no-idx
```

## Archive integrity maintenance

```bash
# Read-only: verify all data-part EC, .000 EC, and IDX state
zpaqoec oec_check compress
# alias: oec_verify

# Self-heal repairable EC damage and recreate missing .000 / disposable IDX
zpaqoec oec_fix compress

# If only IDX was manually deleted/staled/corrupted
zpaqoec oec_idx ensure compress --idx X:/ZpaqCache/compress.idx
```

`oec_fix` preserves any replaced damaged data part as `*.oec-bad[.N]`. A structurally unreadable EC sidecar is not automatically rewritten unless `--rebuild-ec` is explicitly supplied, because doing so asserts that the current data bytes are trusted.

## Optimized metadata reads

Fast mmap path:

```bash
zpaqoec oec_l compress --idx X:/ZpaqCache/compress.idx
zpaqoec oec_i compress --idx X:/ZpaqCache/compress.idx
```

If cache is absent/stale/corrupt, the default behavior rebuilds it from `.000` and continues. To forbid automatic rebuild:

```bash
zpaqoec oec_l compress --idx X:/ZpaqCache/compress.idx --idx-no-rebuild
```

To bypass `.idx` entirely:

```bash
zpaqoec oec_l compress --no-idx
```

Native-option variants preserve upstream semantics and parse `.000` directly:

```bash
zpaqoec oec_l compress -all --idx X:/ZpaqCache/compress.idx
```

## Extraction

```bash
zpaqoec oec_x compress path/to/file -to restore
zpaqoec oec_e compress path/to/file
```

`.000` contains metadata but deliberately omits compressed D blocks, so payload still comes from normal data parts. 0.5.0 does not yet bypass upstream multipart extraction with a fragment locator backend.

## EC commands

```bash
zpaqoec ec create compress.001
zpaqoec ec verify compress.001
zpaqoec ec repair compress.001 --output compress.001.repaired
zpaqoec ec info compress.001.ec
```

Default EC geometry:

```text
shard size          64 KiB
data shards         32
parity shards        2 (P + Q over GF(256))
stripes/window      64
nominal parity       6.25%
```

## Build

Linux:

```bash
./scripts/build-linux.sh /path/to/zpaqfranz.cpp
```

Windows / MSYS2 UCRT64:

For a repository that has previously been built with an older injector hotfix, refresh the pristine 64.8 monolith once before rebuilding:

```powershell
.\scripts\fetch-upstream.ps1
```

Then build:

```powershell
.\scripts\build-windows.ps1 `
  -Compiler 'C:\Programs\msys64\ucrt64\bin\g++.exe'
```

The Windows build runs runtime smoke gates for no-arg help, `oec_h`, `oec_version`, and argument-sensitive `oecinit` dispatch before reporting success.

0.5.0 also explicitly uses the Windows CRT `<io.h>` / `<sys/stat.h>` directory APIs for JSON/MD5 filesystem traversal. This fixes current MSYS2 UCRT64 builds where `_finddata64_t` is not a complete public type spelling; OEC uses `_finddata_t` because traversal only needs file names and attributes.

Current identity:

```text
zpaqoec oec_version
zpaqoec OEC overlay 0.5.0 (Optimize + Error Correction)
```

## Tests

```bash
g++ -std=c++11 -O2 src/zfec_cli.cpp -o zfec
./tests/run_injector_tests.sh
./tests/run_ec_tests.sh
./tests/run_oec_a_tests.sh
./tests/run_chunk_compat_tests.sh
./tests/run_maintenance_tests.sh
./tests/run_oecinit_tests.sh
./tests/run_oec_command_tests.sh
./tests/run_idx_tests.sh
./tests/run_windows_script_static_tests.sh
```


### zpaqfranz list-format compatibility

0.5.0 accepts multiple `-terse` layouts and falls back to `-all -terse` + current-state collapse when necessary.


## Deep IDX dedup (0.5.0)

`oec_a` enables deep dedup automatically when an IDX2 cache is available. Lookup order is RAM hot cache -> mmap FRAGMENT_TABLE -> SSD/NVMe. `--idx-memory auto` is the default; use `--idx-memory 0`, `--idx-memory 512M`, `--idx-memory 8G`, etc. Generation-based publication makes commit O(1): new fragment slots are visible inside the current add but are not published to a later process until the native ZPAQ transaction reaches its normal return path. A crash leaves an uncommitted generation which is ignored/reused later. Metadata refreshes preserve the deep EOF section. `--no-idx` disables deep lookup and uses upstream RAM dedup.

### Windows Unicode path parity (r5)

OEC now keeps zpaqfranz's UTF-8 path model across its own ignore/EC/IDX/JSON helpers and converts to UTF-16 Win32 filesystem/process APIs at OS boundaries. Unicode source paths such as `Kỳ's World (R.A.N.D)` no longer pass through narrow `_findfirst/_spawnv` APIs.
