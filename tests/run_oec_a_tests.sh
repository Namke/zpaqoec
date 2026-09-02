#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$ROOT/tests/oec-a-test-run"
rm -rf "$TMP"; mkdir -p "$TMP/extensions"
cp "$ROOT/tests/fake_upstream.cpp" "$TMP/fake.cpp"
python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/fake.cpp" --extension-dir "$TMP/extensions" >/dev/null
# idempotence
python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/fake.cpp" --extension-dir "$TMP/extensions" >/dev/null
[[ $(grep -c 'ZPAQOEC_BRIDGE_DECL' "$TMP/fake.cpp") -eq 1 ]]
[[ $(grep -c '^#include "extensions/zpaqfranz_ext.hpp"$' "$TMP/fake.cpp") -eq 1 ]]
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
# Explicit wildcard pattern must be honored as-is (not treated as a bare base).
rm -f Documents*.zpaq Documents*.ecstate Documents*.ec Documents*.idx fake_calls.log
./zpaqfranz oec_a 'Documents?????.zpaq' dummy-source --ec-data 16 --ec-stripes 8 >/tmp/oec-pattern.out
[ -f Documents00001.zpaq ] && [ -f Documents00001.zpaq.ec ]
[ -f Documents00000.zpaq ] && [ -f Documents00000.zpaq.ec ]
[ -f Documents.ecstate ]
grep -q 'index=Documents00000.zpaq next=Documents00001.zpaq' /tmp/oec-pattern.out
# -chunk must fail in OEC preflight, before native add, with an actionable reason.
set +e
./zpaqfranz oec_a 'Chunk?????.zpaq' dummy-source -chunk 4g >/tmp/oec-chunk.out 2>&1
rc=$?
set -e
[ "$rc" -eq 2 ]
grep -q 'rejects -chunk together with -index' /tmp/oec-chunk.out
[ ! -f Chunk00001.zpaq ]
echo "OEC_A PATTERN/CHUNK PREFLIGHT TESTS PASS"
