# Upstream integration

OEC is injected into the zpaqfranz monolith with one late include (immediately before `main()`) and one early dispatcher inside `main()`.

The original zpaqfranz commands remain untouched. OEC commands recursively invoke the same executable with native commands after normalizing OEC archive layout.

## Public routing

```text
oecinit -> native x PATTERN -index ZERO (index construction), then EC generation
oec_a   -> native a PATTERN ... -index ZERO, then EC generation
oec_l   -> native l ZERO ...
oec_i   -> native i ZERO ...
oec_x   -> native x PATTERN ...
oec_e   -> native e PATTERN ...
```

This split is intentional. The ZPAQ zero-part index contains metadata but no D blocks, so metadata-only operations can read it directly; extraction still needs data parts.

## Future `.idx`

The disk-backed `.idx` cache must remain non-authoritative. It is allowed to accelerate OEC commands, but deleting it must never make the archive unrecoverable. `000` remains the portable metadata source of truth.
