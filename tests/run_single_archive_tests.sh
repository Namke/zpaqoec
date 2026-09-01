#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$ROOT/tests/single-archive-test-run"
rm -rf "$TMP"; mkdir -p "$TMP/extensions"
cp "$ROOT/tests/fake_upstream.cpp" "$TMP/fake.cpp"
python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/fake.cpp" --extension-dir "$TMP/extensions" >/dev/null
g++ -std=c++11 -O2 "$TMP/fake.cpp" -o "$TMP/zpaqfranz"
cd "$TMP"

# Legacy single-file archive: no .001 part exists.
python3 - <<'PY'
with open('single.zpaq','wb') as f:
    f.write(bytes((i*23+11)&255 for i in range(2*1024*1024+333)))
PY
orig_size=$(wc -c < single.zpaq)
./zpaqfranz oec_init single.zpaq --ec-data 16 --ec-stripes 8 > init.out

grep -Fq 'mode=single' init.out
[ -f single.zpaq ]
[ -f single.zpaq.ec ]
[ -f single.000.zpaq ]
[ -f single.000.zpaq.ec ]
[ -f single.idx ]
[ ! -e single.zpaq.001 ]
[ ! -e single.ecstate ]
[ "$(wc -c < single.zpaq)" -eq "$orig_size" ]
./zpaqfranz ec verify single.zpaq >/dev/null
./zpaqfranz ec verify single.000.zpaq >/dev/null
./zpaqfranz oec_idx verify single.zpaq >/dev/null

# Metadata defaults are served from the single archive's mmap cache.
: > native_calls.log
./zpaqfranz oec_l single.zpaq >/dev/null
./zpaqfranz oec_i single.zpaq >/dev/null
[ ! -s native_calls.log ] || { echo 'single oec_l/i unexpectedly invoked native parser'; cat native_calls.log; exit 1; }
# Option-rich metadata uses only the zero index.
./zpaqfranz oec_l single.zpaq -all >/dev/null
grep -Fxq 'l|single.000.zpaq|-all' native_calls.log
# Extraction reads the exact single payload, not a synthesized .??? pattern.
./zpaqfranz oec_x single.zpaq file.txt -to out >/dev/null
./zpaqfranz oec_e single.zpaq file.txt >/dev/null
grep -Fxq 'x|single.zpaq|file.txt|-to|out' native_calls.log
grep -Fxq 'e|single.zpaq|file.txt' native_calls.log

# Single-file add appends to the same archive, uses .000, and regenerates EC.
before_add=$(wc -c < single.zpaq)
./zpaqfranz oec_a single.zpaq dummy-source --ec-data 16 --ec-stripes 8 --idx-refresh >/dev/null
after_add=$(wc -c < single.zpaq)
[ "$after_add" -gt "$before_add" ]
[ ! -e single.zpaq.001 ]
./zpaqfranz ec verify single.zpaq >/dev/null
./zpaqfranz ec verify single.000.zpaq >/dev/null
./zpaqfranz oec_idx verify single.zpaq >/dev/null
grep -Fq 'a|single.zpaq|dummy-source|-index|single.000.zpaq' native_calls.log

# Minimum guarantee: even if native zero-index creation fails, data EC survives.
python3 - <<'PY'
with open('failindex.zpaq','wb') as f:
    f.write(bytes((i*29+3)&255 for i in range(1024*1024+99)))
PY
set +e
./zpaqfranz oecinit failindex.zpaq --ec-data 16 --ec-stripes 8 > fail.out 2>&1
rc=$?
set -e
[ "$rc" -ne 0 ]
[ -f failindex.zpaq.ec ]
[ ! -e failindex.000.zpaq ]
grep -Fq 'single archive EC is ready, but zero-part index rebuild failed' fail.out
./zpaqfranz ec verify failindex.zpaq >/dev/null

echo 'OEC SINGLE ARCHIVE TESTS PASS'
