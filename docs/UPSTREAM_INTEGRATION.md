# Upstream integration

zpaqoec is an overlay on the zpaqfranz monolith. It keeps all native zpaqfranz commands and archive bytes unchanged, and adds an OEC dispatcher plus external `.ec` and `.idx` files.

The injector deliberately separates a lightweight dispatcher bridge from the heavy OEC headers so platform feature macros and compatibility wrappers in zpaqfranz are established first. Eligible `main(argc, argv)` and `zpaq_main_internal(argc, argv)` entry points call the bridge; no-argument `main()` entry points are not patched.

## Public routing

```text
oecinit / oec_init
    native x PATTERN -index ZERO      build/rebuild .000 when needed
    EC creation                       create missing/forced sidecars
    IDX build/reuse                   create or validate disposable mmap cache

oec_a
    no -chunk: native a PATTERN ... -index ZERO
    with -chunk: native a PATTERN ... -chunk ...  exact upstream archive-writing path
                 native x PATTERN -index TMP      rebuild ZERO after commit
    EC creation                       protect every newly committed physical part
    IDX lifecycle                     leave stale for lazy rebuild, or refresh with --idx-refresh

oec_l
    valid IDX + default query         return LIST section directly from mmap cache
    cache miss/stale/corrupt          rebuild from ZERO, then use mmap
    native l-specific options         native l ZERO ... for exact upstream semantics

oec_i
    valid IDX + default query         return INFO section directly from mmap cache
    cache miss/stale/corrupt          rebuild from ZERO, then use mmap
    native i-specific options         native i ZERO ... for exact upstream semantics

oec_x / oec_e
    validate/use OEC metadata layer where applicable
    native x/e PATTERN ...            payload still comes from normal ZPAQ parts
```

The ZPAQ zero-part index is the portable metadata source of truth. It contains the journal metadata required to reconstruct the archive catalog but intentionally omits compressed D payload blocks. Therefore metadata-only operations can be accelerated without reading data parts, while extraction still requires the actual multipart data files.

### Native `-chunk` compatibility

zpaqfranz 64.8 rejects `-chunk` together with `-index`. OEC does not bypass that guard by changing the writer. Instead, for a chunked add it invokes the same native `a PATTERN ... -chunk ...` command an upstream user would invoke. This preserves fragmenting, dedup decisions, compression, block construction, chunk rollover, and physical `.zpaq` output. Only after the add succeeds does OEC run a metadata-only `x PATTERN -index TMP -force` pass and atomically install the rebuilt `.000`.

This is intentionally asymmetric: the archive bytes are produced by the untouched upstream writer; the portable zero-part is derived afterward. A chunked update may create multiple new files, and OEC protects the complete new range. The final short part of a completed update is never filled by a later update.

The regression gate verifies that the native add call contains `-chunk` but not `-index`, that the next call is the post-commit index rebuild, and that a second add leaves the previous final part byte-identical while starting at the next number.

## Password preflight bridge

0.5.0 adds a non-invasive pre-parser authentication preflight. It does not modify ZPAQ encryption or key derivation. When `PASSWORD_FOLDER` supplies a matching plaintext password file, zpaqoec sets process-local `FRANZKEY`; upstream remains responsible for all AES authentication/decryption exactly as before. If no password file matches, the bridge returns to normal upstream interactive behavior.

## `.idx` integration in 0.5.0

The `.idx` cache is implemented as a versioned, memory-mapped, transactional sidecar. It is non-authoritative and may live on a local SSD/NVMe independently of the archive. Its source fingerprint is derived from `.000`; if the fingerprint or section checksums do not validate, OEC rejects the cache and falls back to/rebuilds it from `.000`.

Current cache sections accelerate the default `oec_l` and `oec_i` paths without invoking the native `.000` parser on a cache hit. `oecinit` creates/reuses the cache, and `oec_a` manages cache freshness.

### Deliberate boundary

0.5.0 patches only the `HTIndex htinv(...)` object inside `Jidac::add()` to `OecHybridHTIndex` with the same `find()`/`update()` API and injects `commit()` on normal return. Compression, HT/DT vectors, fragment IDs and ZPAQ writer remain upstream. The adapter uses IDX2 when enabled and instantiates native `HTIndex` on any deep-backend failure. `oec_x/oec_e` still do not yet use a structured fragment->part/block locator.

A later deep-integration milestone may add a disk-backed fragment/hash backend, but it must preserve upstream dedup semantics and retain `.000` as the recovery authority. The `.idx` file must never become necessary for archive recovery.

### Encrypted IDX behavior (0.5.0)

OECIDX2 stores structured FILE_TABLE/STRING_POOL/PATH_HASH metadata plus materialized LIST/INFO compatibility views. Standard AES-encrypted zero parts are detected from the ZPAQ encrypted-stream prefix rule; automatic plaintext cache creation/use is disabled unless `--idx-plaintext` is explicitly selected. This also fixes a 0.3.1 dead-wait where captured native `l`/`i` output hid their password prompts.

