# OEC IDX format — OECIDX2 (0.5.0)

`.idx` is a disposable acceleration cache. The `.000` ZPAQ metadata index is authoritative; deleting `.idx` never makes the archive unrecoverable.

## Commands

```bash
zpaqoec oec_idx verify  compress --idx X:/Fast/compress.idx
zpaqoec oec_idx ensure  compress --idx X:/Fast/compress.idx
zpaqoec oec_idx upgrade compress --idx X:/Fast/compress.idx
zpaqoec oec_idx rebuild compress --idx X:/Fast/compress.idx
zpaqoec oec_idx info    compress --idx X:/Fast/compress.idx
zpaqoec oec_idx drop    compress --idx X:/Fast/compress.idx
```

`verify` validates the cache and the zero-part fingerprint. A valid old IDX1 is recognized but reported `current=no` and returns nonzero. `ensure` is the normal self-heal operation: keep a valid current IDX2, otherwise rebuild. `upgrade` rebuilds a valid old cache to the current format and is a no-op for a current IDX2. `rebuild` always regenerates from `.000`.

## OECIDX2 sections

The v2 header stores the `.000` size/mtime plus CRC32C samples at the beginning/middle/end and CRC32C for every populated section.

```text
OECIDX2 header
LIST                 materialized native default l view (compatibility)
INFO                 materialized native i view (compatibility)
FILE_TABLE           fixed-size structured current-file records
STRING_POOL          paths, modification timestamps, attributes
PATH_HASH            sorted FNV-1a(path) -> FILE_TABLE index
FRAGMENT_TABLE       reserved, empty in 0.5.0
BLOCK_TABLE          reserved, empty in 0.5.0
```

`FILE_TABLE` contains path offset/length, size, modification timestamp, attributes, type, current version, status and compression ratio. `PATH_HASH` is mmap-searchable and verifies each index points to a valid file record.

## Compatibility

The reader accepts both `OECIDX1` and `OECIDX2`. New builds write IDX2. IDX1 contains LIST/INFO only. Use `oec_idx upgrade` or `ensure` to move it to IDX2 without touching archive parts.

## Current RAM/dedup boundary

0.5.0 activates the reserved `FRAGMENT_TABLE` as a mutable open-addressed SHA-1 -> fragment-id index used by `oec_a`. A small `DeepMeta` header stores capacity, committed/last generation, indexed-through fragment ID and committed count. Each slot stores SHA-1, fragment ID, uncompressed size, generation and per-slot CRC32C. Metadata sections remain transactional/rebuildable; metadata refresh preserves the deep EOF section. `BLOCK_TABLE` remains reserved for future direct extraction seeking.

## Encrypted archives

Structured IDX still exposes filenames/metadata in plaintext. Automatic plaintext cache creation remains disabled for standard AES-encrypted `.000` unless `--idx-plaintext` is explicitly supplied. Password resolution continues to support explicit key, `FRANZKEY`, `PASSWORD_FOLDER`, then interactive input.

### Deep generation semantics

At adapter open, `active_generation = ++last_generation` is persisted. Existing slots with generation <= `committed_generation` are visible; slots from the active generation are visible only to that process so duplicate chunks within the same add deduplicate. Normal `Jidac::add()` return calls `htinv.commit()`, which publishes the active generation by updating `committed_generation` and `indexed_through`. A crash does not advance committed generation; abandoned slots are invisible and become reusable tombstones.
