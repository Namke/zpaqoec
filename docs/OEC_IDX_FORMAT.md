# OEC `.idx` cache format — v1

The OEC `.idx` file is a **disposable acceleration cache**. It is not part of ZPAQ format and is never authoritative.

## Authority and recovery

```text
compress.000  authoritative, portable ZPAQ metadata index
compress.idx  optional, local, rebuildable cache
```

If `.idx` is missing, stale, corrupted, located on a failed SSD, or manually deleted, the archive remains recoverable. Rebuild with:

```bash
zpaqoec oec_idx build compress --idx /fast/cache/compress.idx
```

## Encryption/privacy policy

`OECIDX1` stores the materialized default `l` and `i` views. These sections are plaintext. For a standard AES-encrypted ZPAQ zero-part, OEC 0.3.4 therefore disables plaintext IDX creation and automatic use by default. The durable encrypted archive, `.000`, and `.ec` files remain unaffected.

Explicit opt-in:

```bash
zpaqoec oec_idx build secret.zpaq --idx /fast/secret.idx --idx-plaintext -key PASSWORD
```

`FRANZKEY` is inherited by the native metadata child processes and avoids repeated interactive prompts. Without a supplied key, explicit plaintext-cache mode shows progress and visibly announces each password wait instead of capturing the prompt. Authentication prompt/mask chatter is removed before writing cache sections.

An `.idx` created by 0.3.1 for an encrypted archive may already contain plaintext metadata. 0.3.4 will not use it automatically unless `--idx-plaintext` is supplied; `oec_idx drop` can remove it.

## Storage / mmap

Version 1 is memory mapped for reads:

- Windows: `CreateFileMappingW` + `MapViewOfFile`;
- Unix/Linux: `mmap(MAP_SHARED)`.

The process maps the file address space; the OS page cache decides which pages remain resident. The cache may therefore be placed on a dedicated SSD/NVMe independently of the archive parts.

## Header

Packed header fields:

```text
magic                "OECIDX1"
version              1
header_size
flags
source_size
source_mtime
source_crc_first
source_crc_middle
source_crc_last
list_offset/list_size/list_crc32c
info_offset/info_size/info_crc32c
created_unix
header_crc32c
```

The current payload sections cache the default native metadata views used by:

```text
LIST section -> oec_l (without native options)
INFO section -> oec_i (without native options)
```

All section ranges and CRC32C values are verified before use.

## Source fingerprint / staleness

The authoritative `.000` is fingerprinted using:

- file size;
- modification time;
- CRC32C sample of first 64 KiB;
- CRC32C sample around the middle;
- CRC32C sample of the last 64 KiB.

A mismatch marks the cache stale. Normal `oec_l/oec_i` can rebuild it automatically unless `--idx-no-rebuild` is specified.

This fingerprint is a cache-validity mechanism, not an archive integrity mechanism. Archive integrity/recovery remains the responsibility of ZPAQ checks plus OEC `.ec` sidecars.

## Transactional replacement

A rebuild writes:

```text
compress.idx.tmp
```

then atomically installs the completed cache, keeping a temporary `.oecidx.bak` only during replacement. A failed rebuild does not turn `.idx` into archive authority.

## Password resolution for encrypted metadata passes

If `PASSWORD_FOLDER` is set and neither explicit `-key`/`-franzen` nor `FRANZKEY` is present, the OEC bridge looks for a normalized `<archive>.password` file before native `l/i/x -index` code can ask interactively. A successful match is exported only as process-local `FRANZKEY`, so IDX child passes inherit it without adding plaintext secrets to command-line arguments.

Password files are external operator secrets and are never copied into `.idx`, `.000`, or `.ec`.

## Current acceleration coverage (0.3.4)

| Command/path | v1 cache use |
|---|---|
| `oec_l` default | direct mmap hit, no native `.000` parse |
| `oec_i` default | direct mmap hit, no native `.000` parse |
| `oec_l/i` with native options | cache may be validated; native `.000` parser used for exact option semantics |
| `oecinit` | build/reuse cache |
| `oec_a` | cache lifecycle; optional immediate `--idx-refresh` |
| `oec_x/e` | validates cache when present; native multipart payload path remains active |

### Deliberate non-claim

v1 does **not** yet replace zpaqfranz's in-memory `Jidac` file/fragment state during `add`. Upstream reconstructs archive state and builds its fragment hash lookup after reading the archive/index. Replacing that safely requires a deeper integration with upstream HT/DT/dedup internals.

Likewise v1 does not yet store a stable fragment-to-part/block locator table for direct extraction bypass. Those are later format sections, not implied by the current `OECIDX1` metadata-view cache.

This boundary is intentional: `.idx` must remain an optimization whose failure can always fall back to standard `.000`/ZPAQ behavior.
