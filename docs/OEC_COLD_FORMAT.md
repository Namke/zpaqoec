# OEC cold format — OECOLD1 / OECPAR1

Cold protection is optional adjunct metadata/parity. It never changes ZPAQ, ZFEC or IDX bytes.

## Coding

Each group has `K` data lanes and `M` parity lanes. OEC builds a Vandermonde `(K+M) x K` matrix over GF(256), multiplies it by the inverse of its first K rows, and therefore obtains a systematic Reed–Solomon generator. Any K good shards in a stripe reconstruct the original K data shards.

Implementation bounds: K=1..128, M=1..32, K+M<=255. Shard size is a power of two from 64 KiB through 16 MiB.

`--grouping size` sorts data parts by size then partitions them into balanced-width groups. `sequential` preserves part order but still balances group counts. This avoids a pathological final group containing only one or two files when the archive count is just above K.

## Manifest OECOLD1

The line-oriented manifest is UTF-8 safe: paths are hex-encoded UTF-8 bytes. It records the data root, parity root, set name, K/M/shard size/profile/grouping/hash policy, every data/parity filename and exact size, per-shard CRC32C values, and optional SHA-256 whole-file hashes. A final `MANIFEST_SHA256=` authenticates accidental corruption of the entire preceding manifest body.

The primary manifest is normally accompanied by two byte-identical `.copyN` replicas. Use `--manifest-copy-dir` to put them on another device/fault domain.

## Parity OECPAR1

Each parity lane is a separate file per group:

```text
<set>.g000001.p001.oecp
<set>.g000001.p002.oecp
...
```

The fixed 128-byte header stores group id, parity lane, K/M, shard size, stripe count and a header CRC32C. Payload is the sequence of fixed-size parity shards. Losing/corrupting one parity file only reduces protection for that group.

## Recovery rule

A stripe is recoverable iff at least K of its K+M data/parity shards pass structural/CRC validation. Repair preflights a group before changing data. If any stripe has fewer than K good shards, repair aborts without installing reconstructed data.

Normal repair writes temporary reconstructed files, preserves each damaged original as `.oec-bad[.N]`, atomically installs repaired data, then regenerates parity for groups whose parity was missing or corrupt. `--no-install` writes `.repaired` files only.

## Storage overhead

For equal-size data lanes and full-width groups, nominal overhead is M/K. Examples: 20+2=10%, 20+3=15%, 32+2=6.25%, 24+4=16.67%. Tail-size variation and group balancing can make actual overhead higher; `oec_cold info` reports actual bytes and percentage.
