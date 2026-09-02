#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$ROOT/tests/maintenance-test-run"; rm -rf "$TMP"; mkdir -p "$TMP/extensions"
cp "$ROOT/tests/fake_upstream.cpp" "$TMP/fake.cpp"
python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/fake.cpp" --extension-dir "$TMP/extensions" >/dev/null
g++ -std=c++11 -O2 "$TMP/fake.cpp" -o "$TMP/zpaqoec"
cd "$TMP"
./zpaqoec oec_a arc chunk-first -chunk 4g --ec-data 16 --ec-stripes 8 --idx-refresh >/dev/null
./zpaqoec oec_check arc --ec-data 16 --ec-stripes 8 >/dev/null

# Disposable IDX can be deleted; read-only check reports maintenance needed,
# while oec_fix recreates it from authoritative .000.
rm -f arc.idx
set +e
./zpaqoec oec_check arc --ec-data 16 --ec-stripes 8 > check-missing-idx.out
RC=$?
set -e
[ "$RC" -eq 2 ]
grep -q 'IDX-NEEDS-REBUILD' check-missing-idx.out
./zpaqoec oec_fix arc --ec-data 16 --ec-stripes 8 >/dev/null
[ -f arc.idx ]
./zpaqoec oec_idx verify arc >/dev/null

# Delete authoritative .000 too: fix must reconstruct it from native part bytes,
# protect it again, then recreate IDX.
rm -f arc.000 arc.000.ec arc.idx
./zpaqoec oec_fix arc --ec-data 16 --ec-stripes 8 >/dev/null
[ -f arc.000 ] && [ -f arc.000.ec ] && [ -f arc.idx ]

# Corrupt one data part. Check should report repairable, fix repairs in place,
# preserves the bad original, and post-fix check is clean.
python3 - <<'PY'
p='arc.002'
with open(p,'r+b') as f:
    f.seek(12345); b=f.read(1); f.seek(12345); f.write(bytes([b[0]^0x5a]))
PY
set +e
./zpaqoec oec_check arc --ec-data 16 --ec-stripes 8 > damaged.out
RC=$?
set -e
[ "$RC" -eq 2 ]
grep -q 'REPAIRABLE arc.002' damaged.out
./zpaqoec oec_fix arc --ec-data 16 --ec-stripes 8 > fixed.out
ls arc.002.oec-bad* >/dev/null
grep -q 'repaired arc.002' fixed.out
./zpaqoec oec_check arc --ec-data 16 --ec-stripes 8 >/dev/null

echo 'OEC MAINTENANCE TESTS PASS'
