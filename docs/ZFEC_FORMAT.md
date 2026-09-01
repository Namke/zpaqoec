# ZFEC v1 on-disk format

ZFEC is an independent repair sidecar. It never changes the protected ZPAQ part.

## File header

v1 uses fixed packed 72-byte file headers and 48-byte window headers and currently requires a little-endian host. Header sizes are compile-time asserted. A future format revision can add explicit byte-wise serialization for big-endian hosts without changing the P/Q coding model.

Header fields:

- magic `ZFEC001`
- version
- flags (`CRC32C`)
- shard size
- data shard count
- parity shard count (v1 = 2)
- stripes per window
- protected file size
- window capacity
- window count
- header CRC32C

## Window

Each window stores:

- window header
- expected CRC32C for every present data shard
- CRC32C for P and Q parity of each stripe
- P parity bytes
- Q parity bytes

The protected file is consumed sequentially. For local shard index `i`:

```text
stripe = i % stripe_count
lane   = i / stripe_count
```

This transposes physical shard order across stripes and provides burst-error interleaving while keeping encoder memory bounded.

## P/Q equations

For byte position `k`:

```text
P[k] = D0[k] xor D1[k] xor ...
Q[k] = c0*D0[k] xor c1*D1[k] xor ...
ci   = alpha^i in GF(2^8), primitive polynomial 0x11d
```

For missing lanes `a,b`:

```text
SP = Da xor Db
SQ = ca*Da xor cb*Db
Db = (SQ xor ca*SP) / (cb xor ca)
Da = SP xor Db
```

Recovered shards are accepted only if their stored CRC32C matches.
