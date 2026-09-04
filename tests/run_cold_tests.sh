#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RUN="$ROOT/tests/cold-test-run"
rm -rf "$RUN" && mkdir -p "$RUN/bin" "$RUN/data" "$RUN/parity" "$RUN/meta" "$RUN/replicas"
CXX="${CXX:-g++}"
"$CXX" -std=c++11 -O2 -I"$ROOT/src" "$ROOT/src/zfext_standalone.cpp" -o "$RUN/bin/zpaqoec-test"
Z="$RUN/bin/zpaqoec-test"

mkparts() {
  local dir="$1" count="$2" prefix="$3"
  python3 - "$dir" "$count" "$prefix" <<'PY'
import sys, pathlib
root=pathlib.Path(sys.argv[1]); count=int(sys.argv[2]); prefix=sys.argv[3]
root.mkdir(parents=True,exist_ok=True)
for i in range(1,count+1):
    n=115000+i*9137
    (root/f'{prefix}{i:05d}.zpaq').write_bytes(bytes(((j*37+i*19)%251 for j in range(n))))
(root/f'{prefix}00000.zpaq').write_bytes(bytes(((j*17+3)%251 for j in range(73000))))
PY
}

# 1) Fully custom split-root seal and healthy verify.
mkparts "$RUN/data" 4 Documents
"$Z" oec_cold seal "$RUN/data/Documents?????.zpaq" --no-index --profile safe --data 4 --parity 2 --shard-size 64K --grouping sequential --output "$RUN/parity" --manifest "$RUN/meta/Documents.oecmanifest" --manifest-copies 2 --manifest-copy-dir "$RUN/replicas" --force >"$RUN/seal.log"
grep -q 'k=4 m=2' "$RUN/seal.log"
"$Z" oec_cold verify "$RUN/meta/Documents.oecmanifest" >"$RUN/verify.log"
grep -q 'unrecoverable=0' "$RUN/verify.log"
"$Z" oec_cold info "$RUN/meta/Documents.oecmanifest" >"$RUN/info.log"
grep -q 'max_k=4 m=2' "$RUN/info.log"

# 2) M=2: lose any two whole parts in the same group and reconstruct byte-identical.
cp "$RUN/data/Documents00001.zpaq" "$RUN/one.orig"
cp "$RUN/data/Documents00003.zpaq" "$RUN/three.orig"
rm "$RUN/data/Documents00001.zpaq" "$RUN/data/Documents00003.zpaq"
set +e
"$Z" oec_cold verify "$RUN/meta/Documents.oecmanifest" >"$RUN/lostverify.log" 2>&1
rc=$?
set -e
[[ $rc -eq 2 ]]
"$Z" oec_cold repair "$RUN/meta/Documents.oecmanifest" >"$RUN/repair.log"
cmp "$RUN/one.orig" "$RUN/data/Documents00001.zpaq"
cmp "$RUN/three.orig" "$RUN/data/Documents00003.zpaq"
"$Z" oec_cold verify "$RUN/meta/Documents.oecmanifest" >/dev/null

# 3) Bitrot in data + lost parity: repair data and regenerate parity.
# Use a second set so the relocation test below retains a pristine first set.
rm -rf "$RUN/bit" && mkdir -p "$RUN/bit/data" "$RUN/bit/par" "$RUN/bit/meta"
mkparts "$RUN/bit/data" 4 Bit
cp "$RUN/bit/data/Bit00002.zpaq" "$RUN/bit/two.orig"
"$Z" oec_cold seal "$RUN/bit/data/Bit?????.zpaq" --no-index --data 4 --parity 2 --shard-size 64K --output "$RUN/bit/par" --manifest "$RUN/bit/meta/Bit.oecmanifest" --force >/dev/null
python3 - "$RUN/bit/data/Bit00002.zpaq" <<'PY'
import sys, pathlib
p=pathlib.Path(sys.argv[1]); b=bytearray(p.read_bytes()); b[70000]^=0x33; p.write_bytes(b)
PY
rm "$(find "$RUN/bit/par" -name '*.p001.oecp' | head -1)"
"$Z" oec_cold repair "$RUN/bit/meta/Bit.oecmanifest" >/dev/null
cmp "$RUN/bit/two.orig" "$RUN/bit/data/Bit00002.zpaq"
"$Z" oec_cold verify "$RUN/bit/meta/Bit.oecmanifest" >/dev/null

# 4) --no-install emits .repaired and leaves damaged original untouched.
rm -rf "$RUN/noinstall" && mkdir -p "$RUN/noinstall/data" "$RUN/noinstall/par" "$RUN/noinstall/meta"
mkparts "$RUN/noinstall/data" 4 NI
"$Z" oec_cold seal "$RUN/noinstall/data/NI?????.zpaq" --no-index --data 4 --parity 2 --shard-size 64K --output "$RUN/noinstall/par" --manifest "$RUN/noinstall/meta/NI.oecmanifest" --force >/dev/null
cp "$RUN/noinstall/data/NI00004.zpaq" "$RUN/noinstall/orig"
rm "$RUN/noinstall/data/NI00004.zpaq"
"$Z" oec_cold repair "$RUN/noinstall/meta/NI.oecmanifest" --no-install >/dev/null
test ! -e "$RUN/noinstall/data/NI00004.zpaq"
cmp "$RUN/noinstall/orig" "$RUN/noinstall/data/NI00004.zpaq.repaired"

# 5) M=3 reconstructs 3 lost data parts.
rm -rf "$RUN/m3" && mkdir -p "$RUN/m3/data" "$RUN/m3/par" "$RUN/m3/meta"
mkparts "$RUN/m3/data" 5 M3
for n in 1 3 5; do cp "$RUN/m3/data/M3$(printf '%05d' "$n").zpaq" "$RUN/m3/$n.orig"; done
"$Z" oec_cold seal "$RUN/m3/data/M3?????.zpaq" --no-index --data 5 --parity 3 --shard-size 64K --output "$RUN/m3/par" --manifest "$RUN/m3/meta/M3.oecmanifest" --force >/dev/null
rm "$RUN/m3/data/M300001.zpaq" "$RUN/m3/data/M300003.zpaq" "$RUN/m3/data/M300005.zpaq"
"$Z" oec_cold repair "$RUN/m3/meta/M3.oecmanifest" >/dev/null
for n in 1 3 5; do cmp "$RUN/m3/$n.orig" "$RUN/m3/data/M3$(printf '%05d' "$n").zpaq"; done

# 6) M+1 is rejected before any install.
rm "$RUN/m3/data/M300001.zpaq" "$RUN/m3/data/M300002.zpaq" "$RUN/m3/data/M300003.zpaq" "$RUN/m3/data/M300004.zpaq"
set +e
"$Z" oec_cold repair "$RUN/m3/meta/M3.oecmanifest" >"$RUN/m3/unrec.log" 2>&1
rc=$?
set -e
[[ $rc -eq 3 ]]
test ! -e "$RUN/m3/data/M300001.zpaq"

# 7) Manifest loss recovery from separate replica fault domain.
rm -f "$RUN/meta/Documents.oecmanifest"
"$Z" oec_cold recover-manifest "$RUN/meta/Documents.oecmanifest" --from "$RUN/replicas/Documents.oecmanifest.copy1" >/dev/null
"$Z" oec_cold info "$RUN/meta/Documents.oecmanifest" >/dev/null

# 8) Relocation overrides: move data and parity independently.
mkdir -p "$RUN/moved-data" "$RUN/moved-parity"
mv "$RUN/data"/Documents*.zpaq "$RUN/moved-data/"
mv "$RUN/parity"/*.oecp "$RUN/moved-parity/"
"$Z" oec_cold verify "$RUN/meta/Documents.oecmanifest" --data-root "$RUN/moved-data" --parity-root "$RUN/moved-parity" >/dev/null

# 9) Include .000 by default and drop hot .ec only after verified seal.
rm -rf "$RUN/drop" && mkdir -p "$RUN/drop/data" "$RUN/drop/par" "$RUN/drop/meta"
mkparts "$RUN/drop/data" 3 Drop
for f in "$RUN/drop/data"/*.zpaq; do printf x > "$f.ec"; done
"$Z" oec_cold seal "$RUN/drop/data/Drop?????.zpaq" --data 4 --parity 2 --shard-size 64K --output "$RUN/drop/par" --manifest "$RUN/drop/meta/Drop.oecmanifest" --drop-part-ec --force >/dev/null
! find "$RUN/drop/data" -name '*.ec' -print -quit | grep -q .
grep -q "44726f7030303030302e7a706171" "$RUN/drop/meta/Drop.oecmanifest"

# 10) UTF-8 source/parity/manifest roots.
U="$RUN/Unicode/Kỳ's World (R.A.N.D)/日本語"
mkdir -p "$U/data" "$U/par" "$U/meta"
mkparts "$U/data" 3 Uni
"$Z" oec_cold seal "$U/data/Uni?????.zpaq" --no-index --data 3 --parity 2 --shard-size 64K --output "$U/par" --manifest "$U/meta/Uni.oecmanifest" --force >/dev/null
"$Z" oec_cold verify "$U/meta/Uni.oecmanifest" >/dev/null

# 11) Parameter guards.
set +e
"$Z" oec_cold seal "$RUN/drop/data/Drop?????.zpaq" --data 250 --parity 10 --no-index > /dev/null 2>&1; a=$?
"$Z" oec_cold seal "$RUN/drop/data/Drop?????.zpaq" --data 4 --parity 2 --shard-size 100K --no-index > /dev/null 2>&1; b=$?
set -e
[[ $a -eq 2 && $b -eq 2 ]]

echo "OEC COLD TESTS PASS"
