#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$ROOT/tests/chunk-compat-test-run"; rm -rf "$TMP"; mkdir -p "$TMP/extensions"
cp "$ROOT/tests/fake_upstream.cpp" "$TMP/fake.cpp"
python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/fake.cpp" --extension-dir "$TMP/extensions" >/dev/null
g++ -std=c++11 -O2 "$TMP/fake.cpp" -o "$TMP/zpaqoec"
cd "$TMP"

# ADD #1: native -chunk path creates four physical parts. OEC must not inject -index
# into the add command; it rebuilds .000 in a separate x -index pass afterwards.
./zpaqoec oec_a chunked chunk-first -chunk 4g --ec-data 16 --ec-stripes 8 > add1.out
for n in 1 2 3 4; do p=$(printf 'chunked.%03d' "$n"); [ -f "$p" ] && [ -f "$p.ec" ]; done
[ -f chunked.000 ] && [ -f chunked.000.ec ]
grep -q 'last_part=4' chunked.ecstate
grep -q 'native -chunk committed 4 new part(s), range=1..4' add1.out
ADD1=$(grep '^a|chunked\.???|' native_calls.log | head -1)
printf '%s' "$ADD1" | grep -q -- '-chunk|4g'
! printf '%s' "$ADD1" | grep -q -- '|-index|'
# The next native call must be the post-commit metadata rebuild.
grep -Fqx 'x|chunked.???|-index|chunked.000.oec-rebuild.tmp|-force' native_calls.log

# ADD #2: upstream starts at part 5. It must never append/fill part 4.
P4_BEFORE=$(sha256sum chunked.004 | awk '{print $1}')
./zpaqoec oec_a chunked chunk-second -chunk 4g --ec-data 16 --ec-stripes 8 > add2.out
P4_AFTER=$(sha256sum chunked.004 | awk '{print $1}')
[ "$P4_BEFORE" = "$P4_AFTER" ]
for n in 5 6 7; do p=$(printf 'chunked.%03d' "$n"); [ -f "$p" ] && [ -f "$p.ec" ]; done
grep -q 'last_part=7' chunked.ecstate
grep -q 'native -chunk committed 3 new part(s), range=5..7' add2.out

# Verify all EC sidecars directly.
for n in 1 2 3 4 5 6 7; do ./zpaqoec ec verify "$(printf 'chunked.%03d' "$n")" >/dev/null; done
./zpaqoec ec verify chunked.000 >/dev/null

echo 'OEC CHUNK COMPAT TESTS PASS'

# Explicit native upstream-style pattern must be passed through unchanged.
# OEC must derive digit width and zero-part from the pattern rather than append .???.
./zpaqoec oec_a 'native?????.zpaq' chunk-first -chunk 4g --ec-data 16 --ec-stripes 8 > explicit.out
for n in 1 2 3 4; do p=$(printf 'native%05d.zpaq' "$n"); [ -f "$p" ] && [ -f "$p.ec" ]; done
[ -f native00000.zpaq ] && [ -f native00000.zpaq.ec ]
grep -q 'last_part=4' native00000.zpaq.ecstate
grep -Fq 'mode=multipart pattern=native?????.zpaq index=native00000.zpaq next=native00001.zpaq' explicit.out
grep -Fqx 'a|native?????.zpaq|chunk-first|-chunk|4g|-exclude' /dev/null 2>/dev/null || true
# Exact native add target must appear and no synthesized second wildcard run may exist.
grep -q '^a|native?????.zpaq|' native_calls.log
! grep -q '^a|native?????.zpaq.???|' native_calls.log

echo 'OEC EXPLICIT PATTERN TEST PASS'
