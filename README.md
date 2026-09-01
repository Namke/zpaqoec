# zpaqfranz-trunkec 0.1.1

Fork overlay for zpaqfranz 64.8 adding:

1. **Trunk/index-first incremental add** through upstream indexed multipart support.
2. **Independent `.ec` sidecars** (`compress.001.ec`) able to detect and repair accidental bitrot.

The extension does not alter the bytes of ZPAQ archive parts. `compress.001`, `compress.002`, ... remain normal ZPAQ multipart files. Recovery information is entirely external.

## Output layout

```text
compress.000          upstream ZPAQ index/trunk
compress.000.ec       EC for the trunk (small and optional)
compress.001          normal ZPAQ part
compress.001.ec       EC sidecar
compress.002
compress.002.ec
compress.ecstate      tiny part-number checkpoint; avoids filename walks on normal runs
```


## Retrofit an existing multipart archive

For archives that were already created without an external trunk/index or EC sidecars:

```bash
zpaqfranz trunkinit "compress.???"
```

The zero part is inferred by replacing `?` with `0`, so the command above creates:

```text
compress.000
compress.000.ec
compress.001.ec
compress.002.ec
...
compress.ecstate
```

The existing `compress.001`, `compress.002`, ... files are **read only** and are never rewritten.
Internally trunk generation uses native ZPAQ semantics:

```text
zpaqfranz x "compress.???" -index compress.000
```

`extract -index` creates the metadata-only index and does not extract payload files. The extension builds the index to a temporary file first and atomically installs it after the native command succeeds.

Generic ZPAQ-style naming is also supported:

```bash
zpaqfranz trunkinit "backup_????????.zpaq"
```

which infers `backup_00000000.zpaq` as the index. Override it when required:

```bash
zpaqfranz trunkinit "backup_????????.zpaq" -index /safe/index/backup.000
```

Encrypted archives pass normal read options through to the native index builder:

```bash
zpaqfranz trunkinit "secret.???" -key PASSWORD
```

By default an existing index is not overwritten. Use `--force` to rebuild the index and regenerate existing EC sidecars. Existing `.ec` files are otherwise skipped.

## Main workflow

```bash
zpaqfranz trunkadd compress /data -method 5
```

Internally this invokes the normal zpaqfranz add path as:

```text
zpaqfranz a compress.??? /data -method 5 -index compress.000
```

After the new part is committed, the extension writes its EC sidecar and refreshes the trunk EC.

The next run reads `compress.ecstate` to know the expected next part. It does **not** scan old part contents. If `.ecstate` is lost while `compress.000` still exists, it performs a one-time filename-only recovery and recreates the state.

### EC commands

```bash
zpaqfranz ec create compress.001
zpaqfranz ec verify compress.001
zpaqfranz ec repair compress.001 --output compress.001.repaired
zpaqfranz ec info compress.001.ec
```

Default repair never overwrites the damaged archive.

## EC v1 format and tolerance

Default geometry:

```text
shard size          64 KiB
data shards         32
parity shards        2 (P + Q over GF(256))
stripes/window      64
window capacity    128 MiB
nominal parity       6.25%
```

Each data shard has CRC32C. Each parity shard also has CRC32C. Metadata has an independent CRC32C.

P is XOR parity. Q is weighted GF(256) parity. This allows exact reconstruction of:

- one bad data shard with either P or Q available;
- two bad data shards when both parity shards are intact;
- one bad data shard even when one parity shard itself has bitrot.

More than two bad data shards in the same stripe are reported as unrecoverable.

### Interleave layout

Within a full 128 MiB window, physical shards rotate across 64 stripes:

```text
lane0: stripe0, stripe1, ... stripe63
lane1: stripe0, stripe1, ... stripe63
...
```

Therefore a contiguous 4 MiB damaged region normally hits one shard per stripe rather than 64 shards in one stripe. A contiguous region up to roughly 8 MiB can normally consume at most the two-parity budget per stripe (alignment and window boundaries still matter).

## Build against upstream

Fetch the pinned 64.8 source if needed:

```bash
./scripts/fetch-upstream.sh
```

Or on Windows PowerShell:

```powershell
.\scripts\fetch-upstream.ps1
```

Put upstream `zpaqfranz.cpp` anywhere, then:

```bash
python3 scripts/apply_to_upstream.py /path/to/zpaqfranz.cpp
g++ -O3 -std=c++11 /path/to/zpaqfranz.cpp -o zpaqfranz -pthread
```

The injector:

- copies `zfec.hpp` and `zpaqfranz_ext.hpp` into a sibling `extensions/` directory;
- adds one include;
- adds one early dispatcher inside `main()`;
- leaves all original commands untouched.

It is idempotent.

Windows/MSYS2:

```powershell
.\scripts\build-windows.ps1 -Upstream C:\src\zpaqfranz\zpaqfranz.cpp
```

Linux:

```bash
./scripts/build-linux.sh /src/zpaqfranz/zpaqfranz.cpp
```

## Current implementation boundary

0.1.0 creates EC **immediately after the ZPAQ part has finalized**, so it performs one sequential reread of the new part. This already satisfies independent sidecar protection and keeps old parts untouched.

The next integration step is to hook the ordered ZPAQ output writer and feed the same output buffers to the EC encoder, eliminating that reread. The EC format is already windowed/stream-friendly, so this does not require a format change.

## Tests

```bash
g++ -std=c++11 -O2 src/zfec_cli.cpp -o zfec
./tests/run_ec_tests.sh
```

Covered now:

- clean create/verify;
- two bad shards in one stripe -> byte-identical repair;
- three bad shards in one stripe -> unrecoverable;
- tail truncation -> recovered;
- one bad data shard + one corrupt parity shard -> recovered;
- AddressSanitizer + UndefinedBehaviorSanitizer pass.

### Windows compiler selection (0.1.3+)

The Windows build script validates the compiler target before compiling. It prefers the upstream-recommended MSYS2 UCRT64 compiler:

```powershell
.\scripts\build-windows.ps1 -Compiler C:\msys64\ucrt64\bin\g++.exe
```

You can also pin it for the current shell:

```powershell
$env:ZPAQFRANZ_GXX='C:\msys64\ucrt64\bin\g++.exe'
.\scripts\build-windows.ps1
```

A valid Windows compiler should report a MinGW target, for example:

```powershell
C:\msys64\ucrt64\bin\g++.exe -dumpmachine
# x86_64-w64-mingw32
```

Do not use a Linux/Cygwin/non-MinGW `g++` to build the Windows binary. If the wrong compiler is selected, upstream takes POSIX code paths and emits errors for `fseeko`, `ftello`, `fileno`, `ftruncate`, `usleep`, `realpath`, `select`, etc.
