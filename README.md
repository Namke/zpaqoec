# zpaqoec 0.2.1

**OEC = Optimize + Error Correction.**

This project is a thin fork/overlay for zpaqfranz 64.8. Its main target is to keep normal ZPAQ archive-part bytes unchanged while adding:

1. a zero-part metadata index (`compress.000`) so optimized metadata/update paths do not scan all old data parts;
2. independent error-correction sidecars (`compress.NNN.ec`);
3. an OEC command namespace that can gain further acceleration (notably the future disk-backed `.idx` cache) without changing the original zpaqfranz commands.

The original commands (`a`, `l`, `i`, `x`, `e`, ...) remain untouched and provide an upstream-compatible baseline.

## OEC archive layout

```text
compress.000          standard ZPAQ metadata-only index
compress.000.ec       EC sidecar for the zero part
compress.001          normal ZPAQ multipart data
compress.001.ec       independent EC sidecar
compress.002
compress.002.ec
...
compress.ecstate      tiny OEC part-number checkpoint

# planned / not in 0.2.1
X:/fast-cache/compress.idx   disposable SSD/NVMe disk-backed acceleration cache
```

`compress.001`, `.002`, ... are not modified by OEC. The `.ec` files are external to ZPAQ format.

## Public OEC commands

Run `zpaqoec` with no parameters to print OEC quick help. A detailed command guide is in [`docs/OEC_COMMANDS.md`](docs/OEC_COMMANDS.md). `oec_init` is accepted as an alias of `oecinit`.

| Command | Purpose | Current read source |
|---|---|---|
| `oecinit` | Retrofit an existing multipart archive with `000` + EC | scans old parts once |
| `oec_a` | Incremental add through the zero-part index + EC | `000` + source filesystem; new data part only |
| `oec_l` | Optimized equivalent of native `l` | **`000` only** |
| `oec_i` | Optimized equivalent of native `i` | **`000` only** |
| `oec_x` | OEC equivalent of native `x` | `000` is OEC authority; payload currently read from multipart data pattern |
| `oec_e` | OEC equivalent of native `e` | same current routing as `oec_x` |
| `ec ...` | EC create/verify/repair/info | target part + sidecar |

The old public extension names `trunkinit` and `trunkadd` are intentionally no longer dispatched.

### Why `oec_l` / `oec_i` can use only `000`

The zero part is a standard ZPAQ index containing the archive metadata but no compressed D blocks. List/info operations need metadata, not file payload, so they can operate directly on the zero part.

### Why `oec_x` / `oec_e` still address the data parts

A ZPAQ index deliberately omits D blocks, so it cannot itself extract file contents. In 0.2.1 `oec_x`/`oec_e` normalize the OEC archive layout but still call the native extraction command on the multipart pattern. When `.idx` is added, these commands will use it for fast file/fragment/part lookup and then open only required data parts where the upstream integration permits it.

## Initialize an existing archive

```bash
zpaqoec oecinit "compress.???"
```

Creates:

```text
compress.000
compress.000.ec
compress.001.ec
compress.002.ec
...
compress.ecstate
```

Existing `compress.001`, `.002`, ... are read-only during retrofit.

Internally index creation uses native ZPAQ semantics:

```text
x "compress.???" -index compress.000
```

Generic naming is supported:

```bash
zpaqoec oecinit "backup_????????.zpaq"
```

Encrypted archive:

```bash
zpaqoec oecinit "secret.???" -key PASSWORD
```

Force regeneration:

```bash
zpaqoec oecinit "compress.???" --force
```

## Incremental add

```bash
zpaqoec oec_a compress /data -method 5
```

Conceptually invokes:

```text
a "compress.???" /data -method 5 -index compress.000
```

then protects the newly committed part and refreshes `compress.000.ec`.

Useful options:

```text
--digits N
--ec-data N
--ec-shard BYTES
--ec-stripes N
--no-index-ec
--no-part-ec
```

For backward compatibility, `--no-trunk-ec` is still accepted internally but is no longer documented as OEC terminology.

## Optimized read commands

Default 3-digit layout:

```bash
zpaqoec oec_l compress -all
zpaqoec oec_i compress
zpaqoec oec_x compress path/to/file -to restore
zpaqoec oec_e compress path/to/file
```

Generic pattern:

```bash
zpaqoec oec_l "backup_????????.zpaq"
zpaqoec oec_x "backup_????????.zpaq" some/file
```

Custom zero-part path:

```bash
zpaqoec oec_l compress --oec-index X:/metadata/compress.000
```

`--oec-index` is an OEC routing option. Native `-index` is rejected on `oec_l/i/x/e` to avoid conflicting with ZPAQ's different `extract -index` meaning.

## EC commands

```bash
zpaqoec ec create compress.001
zpaqoec ec verify compress.001
zpaqoec ec repair compress.001 --output compress.001.repaired
zpaqoec ec info compress.001.ec
```

Default EC geometry remains:

```text
shard size          64 KiB
data shards         32
parity shards        2 (P + Q over GF(256))
stripes/window      64
nominal parity       6.25%
```

## `.idx` roadmap contract

`.idx` is **not implemented in 0.2.1**. The OEC command router is centralized so the future cache can be inserted without changing public commands.

Target behavior:

```text
compress.000     authoritative, portable, recoverable
compress.idx     disposable, rebuildable, SSD/NVMe-friendly
```

When implemented:

- `oec_l` / `oec_i`: prefer `.idx`, fallback to `000`;
- `oec_a`: use disk-backed/mmap fragment/file lookup where possible, with `000` remaining authoritative;
- `oec_x` / `oec_e`: use `.idx` to resolve file -> fragments -> required part/block locations before reading payload;
- stale/missing `.idx`: discard/rebuild, never compromise archive recoverability.

## Build

Linux:

```bash
./scripts/build-linux.sh /path/to/zpaqfranz.cpp
# default output: build/zpaqoec
```

Windows / MSYS2 UCRT64:

```powershell
.\scripts\build-windows.ps1 -Compiler 'C:\Programs\msys64\ucrt64\bin\g++.exe'
# default output: build\zpaqoec.exe
```

The injector is idempotent and migrates 0.1.x patched sources: it relocates the extension include to just before upstream `main()` and converts the old `ZPAQFRANZ_TRUNKEC_DISPATCH` marker to `ZPAQFRANZ_OEC_DISPATCH` without duplicating the hook.

## Tests

```bash
g++ -std=c++11 -O2 src/zfec_cli.cpp -o zfec
./tests/run_ec_tests.sh
./tests/run_injector_tests.sh
./tests/run_oec_a_tests.sh
./tests/run_oecinit_tests.sh
./tests/run_oec_command_tests.sh
```


## Windows dispatcher verification (0.2.2+)

The Windows build is not considered successful merely because MinGW produced an
`.exe`. The build script now executes the freshly built binary and requires all
of these runtime gates to pass:

```powershell
zpaqoec.exe            # must print OEC quick help
zpaqoec.exe oec_h      # must print OEC quick help
zpaqoec.exe oec_version
# zpaqoec OEC overlay 0.2.4 (Optimize + Error Correction)
```

The injector uses a lightweight forward-declared OEC bridge at executable entry.
Only entry signatures that actually expose both `argc` and `argv` are
instrumented; platform-specific `int main()` definitions with no parameters are
left untouched. Every eligible `zpaq_main_internal(...)` definition is
instrumented, so conditional Windows/Linux parser branches are covered.

The heavy extension include and bridge implementation are appended at EOF,
after zpaqfranz's platform compatibility code and outside conditional entry
branches. This avoids both MinGW/UCRT include-order conflicts and a bridge being
compiled only on the wrong platform branch.

The default Windows output is the repository build file, for example:

```text
W:\Works\Github\zpaqoec\build\zpaqoec.exe
```

If a bare `zpaqoec.exe` resolves to another copy such as
`C:\Programs\zpaqoec.exe`, the build script prints a warning. Test the exact
fresh build by full path or install it explicitly:

```powershell
.\scripts\build-windows.ps1 `
  -Compiler 'C:\Programs\msys64\ucrt64\bin\g++.exe' `
  -InstallTo 'C:\Programs\zpaqoec.exe'
```

### Windows runtime smoke gate (0.2.4)

`build-windows.ps1` validates the freshly built executable with no-arg help, `oec_h`, `oec_version`, and an argument-sensitive `oecinit` usage probe. The latter must return exit code 2 and print OEC initialization usage, so the build cannot pass merely because no-arg help happens to contain a common banner.
