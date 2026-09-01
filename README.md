# zpaqfranz-trunkec 0.1.0

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
