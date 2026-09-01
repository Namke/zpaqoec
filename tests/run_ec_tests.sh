#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$ROOT/tests/ec-test-run"
rm -rf "$TMP"; mkdir -p "$TMP"
head -c 20983865 /dev/urandom > "$TMP/original.bin"
cp "$TMP/original.bin" "$TMP/data.bin"
"$ROOT/zfec" create "$TMP/data.bin" --force >/dev/null
"$ROOT/zfec" verify "$TMP/data.bin" >/dev/null

# Default geometry for this file: 321 shards / 32 => 11 stripes.
# Shards 0 and 11 are lane 0/1 of the same stripe (stripe 0).
python3 - "$TMP/data.bin" <<'PY'
import sys
p=sys.argv[1]
with open(p,'r+b') as f:
    for shard,delta in [(0,1234),(11,2222)]:
        off=shard*65536+delta
        f.seek(off); b=f.read(1); f.seek(off); f.write(bytes([b[0]^0xA5]))
PY
set +e
"$ROOT/zfec" verify "$TMP/data.bin" >/dev/null
rc=$?
set -e
[ "$rc" -eq 2 ]
"$ROOT/zfec" repair "$TMP/data.bin" --output "$TMP/repaired.bin" >/dev/null
cmp "$TMP/original.bin" "$TMP/repaired.bin"

# Three bad shards in one stripe must be reported unrecoverable.
cp "$TMP/original.bin" "$TMP/three.bin"
cp "$TMP/data.bin.ec" "$TMP/three.bin.ec"
python3 - "$TMP/three.bin" <<'PY'
import sys
p=sys.argv[1]
with open(p,'r+b') as f:
    for shard in (0,11,22):
        off=shard*65536+333
        f.seek(off); b=f.read(1); f.seek(off); f.write(bytes([b[0]^0x3C]))
PY
set +e
out=$("$ROOT/zfec" verify "$TMP/three.bin" 2>&1); rc=$?
set -e
[ "$rc" -eq 3 ]
echo "$out" | grep -q 'unrecoverable=1'
set +e
"$ROOT/zfec" repair "$TMP/three.bin" --output "$TMP/should-not-exist.bin" >/dev/null 2>&1
rrc=$?
set -e
[ "$rrc" -ne 0 ]
[ ! -e "$TMP/should-not-exist.bin" ]

# Tail truncation by one shard is recoverable.
cp "$TMP/original.bin" "$TMP/trunc.bin"
cp "$TMP/data.bin.ec" "$TMP/trunc.bin.ec"
python3 - "$TMP/trunc.bin" <<'PY'
import os,sys
p=sys.argv[1]
s=os.path.getsize(p)
os.truncate(p,s-65536)
PY
set +e
"$ROOT/zfec" verify "$TMP/trunc.bin" >/dev/null
trc=$?
set -e
[ "$trc" -eq 3 ] # size mismatch is deliberately a stronger status, even if repairable
"$ROOT/zfec" repair "$TMP/trunc.bin" --output "$TMP/trunc.repaired" >/dev/null
cmp "$TMP/original.bin" "$TMP/trunc.repaired"


# One data shard + one damaged parity shard remains recoverable.
cp "$TMP/original.bin" "$TMP/paritycase.bin"
cp "$TMP/data.bin.ec" "$TMP/paritycase.bin.ec"
python3 - "$TMP/paritycase.bin" "$TMP/paritycase.bin.ec" <<'PY2'
import sys
p=sys.argv[1]
with open(p,'r+b') as f:
    f.seek(12345); b=f.read(1); f.seek(12345); f.write(bytes([b[0]^0x77]))
p=sys.argv[2]
with open(p,'r+b') as f:
    f.seek(-1,2); b=f.read(1); f.seek(-1,2); f.write(bytes([b[0]^0x22]))
PY2
set +e
pout=$("$ROOT/zfec" verify "$TMP/paritycase.bin" 2>&1); prc=$?
set -e
[ "$prc" -eq 2 ]
echo "$pout" | grep -q 'bad_parity=1'
"$ROOT/zfec" repair "$TMP/paritycase.bin" --output "$TMP/paritycase.repaired" >/dev/null
cmp "$TMP/original.bin" "$TMP/paritycase.repaired"

echo "EC TESTS PASS"
