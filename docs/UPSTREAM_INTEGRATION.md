# Upstream integration notes

Target baseline: zpaqfranz 64.8.

The extension intentionally reuses zpaqfranz/ZPAQ indexed multipart behavior rather than creating a second catalog format.

Expected upstream capability:

```text
zpaqfranz a "compress.???" SOURCES... -index compress.000
```

The extension command:

```text
zpaqfranz trunkadd compress SOURCES...
```

is an orchestration layer around that existing path.

## Why no custom trunk format

A ZPAQ index contains the journal/catalog information needed for subsequent updates without the compressed D-block payload. Keeping this mechanism means:

- dedup/version semantics remain upstream semantics;
- normal archive parts stay compatible;
- a lost index can be rebuilt from archive parts using upstream recovery tooling;
- this fork only owns EC sidecars and the tiny `.ecstate` checkpoint.

## Writer-tee milestone

0.1.0 generates `part.ec` after close. For zero reread I/O, the upstream ordered output writer should call an EC sink with exactly the same bytes sent to the part file.

Required API shape:

```cpp
EcStreamSink ec(...);
part.write(buf, n);
ec.consume(buf, n);
```

At part close:

```cpp
ec.finalize();
atomic_rename(part_ec_tmp, part_ec);
```

The trunk/index commit should happen only after the part and sidecar have been finalized (or recovery startup must detect an EC-missing committed part and regenerate the sidecar).
