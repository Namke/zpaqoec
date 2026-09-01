#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$ROOT/tests/idx-test-run"
rm -rf "$TMP"; mkdir -p "$TMP/extensions" "$TMP/ssd"
cp "$ROOT/tests/fake_upstream.cpp" "$TMP/fake.cpp"
python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/fake.cpp" --extension-dir "$TMP/extensions" >/dev/null
g++ -std=c++11 -O2 "$TMP/fake.cpp" -o "$TMP/zpaqoec"
cd "$TMP"

./zpaqoec oec_a compress dummy-source --no-idx --ec-data 16 --ec-stripes 8 >/dev/null
./zpaqoec oec_idx build compress --idx "$TMP/ssd/compress.idx" >/dev/null
./zpaqoec oec_idx verify compress --idx "$TMP/ssd/compress.idx" >/dev/null
./zpaqoec oec_idx info compress --idx "$TMP/ssd/compress.idx" | grep -Fq 'OECIDX v1'

# mmap hit: default metadata reads must not invoke upstream.
: > native_calls.log
./zpaqoec oec_l compress --idx "$TMP/ssd/compress.idx" >/dev/null
./zpaqoec oec_i compress --idx "$TMP/ssd/compress.idx" >/dev/null
[ ! -s native_calls.log ] || { echo 'idx mmap hit called native parser'; cat native_calls.log; exit 1; }

# Stale source: verification fails, normal metadata read lazily rebuilds cache.
printf 'changed\n' >> compress.000
if ./zpaqoec oec_idx verify compress --idx "$TMP/ssd/compress.idx" >/dev/null 2>&1; then
  echo 'stale idx unexpectedly verified'; exit 1
fi
: > native_calls.log
./zpaqoec oec_l compress --idx "$TMP/ssd/compress.idx" >/dev/null
grep -Fxq 'l|compress.000' native_calls.log
grep -Fxq 'i|compress.000' native_calls.log
./zpaqoec oec_idx verify compress --idx "$TMP/ssd/compress.idx" >/dev/null

# Corrupt cache: CRC/header validation rejects it and lazy rebuild self-heals.
python3 - "$TMP/ssd/compress.idx" <<'PY'
import sys
p=sys.argv[1]
with open(p,'r+b') as f:
    f.seek(-1,2); b=f.read(1); f.seek(-1,2); f.write(bytes([b[0]^0x5a]))
PY
if ./zpaqoec oec_idx verify compress --idx "$TMP/ssd/compress.idx" >/dev/null 2>&1; then
  echo 'corrupt idx unexpectedly verified'; exit 1
fi
./zpaqoec oec_i compress --idx "$TMP/ssd/compress.idx" >/dev/null
./zpaqoec oec_idx verify compress --idx "$TMP/ssd/compress.idx" >/dev/null

./zpaqoec oec_idx drop compress --idx "$TMP/ssd/compress.idx" >/dev/null
[ ! -e "$TMP/ssd/compress.idx" ]
echo 'OEC IDX TESTS PASS'
