# zpaqoec OEC command guide

OEC means **Optimize + Error Correction**. The OEC command namespace is the main target of this fork. Original zpaqfranz commands remain available unchanged as an upstream-compatible baseline.

## Archive layout

For the conventional 3-digit layout:

```text
compress.000          standard ZPAQ metadata-only index (authoritative OEC zero part)
compress.000.ec       EC sidecar for the zero part
compress.001          normal ZPAQ data part
compress.001.ec       independent EC sidecar
compress.002
compress.002.ec
...
compress.ecstate      small OEC next-part checkpoint
```

The OEC extension does **not** change the ZPAQ bytes in `.001`, `.002`, ... . EC data is stored only in independent `.ec` sidecars.

A future `compress.idx` SSD/NVMe cache is planned but is **not implemented in 0.2.1**. The `.000` file remains authoritative and portable.

## Quick help

Run without parameters:

```bash
zpaqoec
```

This prints the OEC quick-help page. Original zpaqfranz full help remains available with:

```bash
zpaqoec h h
```

You may also print OEC quick help explicitly:

```bash
zpaqoec oec_help
zpaqoec oec_h
```

## `oecinit` / `oec_init`

Both spellings are accepted and are equivalent:

```bash
zpaqoec oecinit "compress.???"
zpaqoec oec_init "compress.???"
```

Purpose:

1. read an existing multipart ZPAQ archive;
2. generate the metadata-only zero part (`compress.000`);
3. generate an EC sidecar for each existing data part;
4. optionally protect the zero part with `compress.000.ec`;
5. seed `compress.ecstate` for subsequent `oec_a` operations.

Existing data parts are read only and are never rewritten by initialization.

Example for an existing archive:

```text
compress.001
compress.002
compress.003
```

Run:

```bash
zpaqoec oecinit "compress.???"
```

Result:

```text
compress.000
compress.000.ec
compress.001
compress.001.ec
compress.002
compress.002.ec
compress.003
compress.003.ec
compress.ecstate
```

Generic multipart names are supported:

```bash
zpaqoec oecinit "backup_????????.zpaq"
```

The inferred zero part is:

```text
backup_00000000.zpaq
```

Override the zero-part destination if required:

```bash
zpaqoec oecinit "backup_????????.zpaq" -index X:/metadata/backup_00000000.zpaq
```

Encrypted archive example:

```bash
zpaqoec oecinit "secret.???" -key PASSWORD
```

Rebuild an existing zero part and regenerate existing EC files:

```bash
zpaqoec oecinit "compress.???" --force
```

EC geometry options:

```text
--ec-data N
--ec-shard BYTES
--ec-stripes N
--no-index-ec
--no-part-ec
```

`--no-trunk-ec` is retained only as a backward-compatible spelling of `--no-index-ec`.

## `oec_a`

Optimized incremental add using the zero-part index:

```bash
zpaqoec oec_a compress /data -method 5
```

For a 3-digit archive this maps conceptually to the native operation:

```text
a "compress.???" /data -method 5 -index compress.000
```

After the new part is committed, OEC creates its EC sidecar and refreshes the zero-part EC sidecar.

Normal operation uses `compress.ecstate` to determine the next part number. If that small checkpoint is missing, OEC performs one filename-only recovery and recreates it.

Useful OEC options:

```text
--digits N
--ec-data N
--ec-shard BYTES
--ec-stripes N
--no-index-ec
--no-part-ec
```

Do not pass native `-index` to `oec_a`; OEC owns the zero-part index path for this command.

## `oec_l`

Optimized equivalent of native `l`:

```bash
zpaqoec oec_l compress
zpaqoec oec_l compress -all
```

For the conventional layout it reads:

```text
compress.000
```

It does **not** need to read `.001`, `.002`, ... because list metadata is available in the zero-part index.

Generic layout:

```bash
zpaqoec oec_l "backup_????????.zpaq"
```

Custom zero-part path:

```bash
zpaqoec oec_l compress --oec-index X:/metadata/compress.000
```

## `oec_i`

Optimized equivalent of native `i`:

```bash
zpaqoec oec_i compress
```

Like `oec_l`, this reads the zero part only for metadata/version information.

```bash
zpaqoec oec_i compress --oec-index X:/metadata/compress.000
```

## `oec_x`

OEC equivalent of native `x`:

```bash
zpaqoec oec_x compress path/to/file -to restore
```

The zero part is the OEC metadata authority, but `.000` intentionally contains no compressed D blocks. Therefore 0.2.1 still passes the multipart data pattern to the native extractor for payload reads.

Generic naming:

```bash
zpaqoec oec_x "backup_????????.zpaq" path/to/file -to restore
```

Custom OEC zero part:

```bash
zpaqoec oec_x compress --oec-index X:/metadata/compress.000 path/to/file -to restore
```

When the planned `.idx` accelerator is implemented, `oec_x` will use it for file -> fragment -> part/block lookup where possible, while `.000` remains authoritative.

## `oec_e`

OEC equivalent of native `e`:

```bash
zpaqoec oec_e compress path/to/file
```

Its current metadata/payload routing follows the same rules as `oec_x`.

## EC commands

Create or replace a sidecar:

```bash
zpaqoec ec create compress.001
```

Verify:

```bash
zpaqoec ec verify compress.001
```

Repair to a separate file:

```bash
zpaqoec ec repair compress.001 --output compress.001.repaired
```

Inspect EC metadata:

```bash
zpaqoec ec info compress.001.ec
```

Default EC geometry in 0.2.1:

```text
shard size          64 KiB
data shards         32
parity shards        2 (P + Q over GF(256))
stripes/window      64
nominal parity       6.25%
```

## Original zpaqfranz commands

The fork does not replace the original command namespace. These still execute upstream behavior:

```bash
zpaqoec a ...
zpaqoec l ...
zpaqoec i ...
zpaqoec x ...
zpaqoec e ...
```

Use the `oec_*` versions when you want OEC routing/optimization/error-correction behavior.

## Windows examples

PowerShell:

```powershell
.\build\zpaqoec.exe oec_init 'W:\LTS\ZPacks\Programs\Games\202601\202601????.zpaq'
.\build\zpaqoec.exe oec_l 'W:\LTS\ZPacks\Programs\Games\202601\202601????.zpaq'
```

Build with MSYS2 UCRT64:

```powershell
.\scripts\build-windows.ps1 -Compiler 'C:\Programs\msys64\ucrt64\bin\g++.exe'
```

The build script relocates older 0.2.0/0.1.x hooks automatically; a clean upstream checkout is not required.

## Linux examples

```bash
./scripts/build-linux.sh
./build/zpaqoec oecinit '/archive/compress.???'
./build/zpaqoec oec_l /archive/compress
```

## Planned `.idx` behavior

The `.idx` accelerator is planned as a disposable disk-backed cache that may live on a fast SSD/NVMe device:

```text
/archive/compress.000             authoritative portable ZPAQ index
/fast-cache/compress.idx          rebuildable acceleration cache
```

After it is implemented, all OEC commands will use it where beneficial:

```text
oec_a  -> fragment/dedup lookup acceleration
oec_l  -> catalog lookup acceleration
oec_i  -> version/info lookup acceleration
oec_x  -> file/fragment/part lookup acceleration
oec_e  -> file/fragment/part lookup acceleration
```

Deleting or losing `.idx` must never make the archive unrecoverable; it will be rebuildable from `.000`.
