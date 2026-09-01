#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$ROOT/tests/oec-a-test-run"
rm -rf "$TMP"; mkdir -p "$TMP/extensions"
cp "$ROOT/tests/fake_upstream.cpp" "$TMP/fake.cpp"
python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/fake.cpp" --extension-dir "$TMP/extensions" >/dev/null
# idempotence
python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/fake.cpp" --extension-dir "$TMP/extensions" | grep -q 'already patched'
g++ -std=c++11 -O2 "$TMP/fake.cpp" -o "$TMP/zpaqfranz"
cd "$TMP"
./zpaqfranz oec_a compress dummy-source --ec-data 16 --ec-stripes 8 >/dev/null
./zpaqfranz oec_a compress dummy-source --ec-data 16 --ec-stripes 8 >/dev/null
[ -f compress.000 ] && [ -f compress.000.ec ]
[ -f compress.001 ] && [ -f compress.001.ec ]
[ -f compress.002 ] && [ -f compress.002.ec ]
grep -q 'last_part=2' compress.ecstate
./zpaqfranz ec verify compress.001 >/dev/null
./zpaqfranz ec verify compress.002 >/dev/null
./zpaqfranz ec verify compress.000 >/dev/null
# Lose state: one-time filename-only recovery should find part 2, then add part 3.
rm compress.ecstate
./zpaqfranz oec_a compress dummy-source --ec-data 16 --ec-stripes 8 | grep -q 'recovered missing .ecstate once'
[ -f compress.003 ] && [ -f compress.003.ec ]
grep -q 'last_part=3' compress.ecstate
echo "OEC_A TESTS PASS"
