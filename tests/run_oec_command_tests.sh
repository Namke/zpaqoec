#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$ROOT/tests/oec-command-test-run"
rm -rf "$TMP"; mkdir -p "$TMP/extensions"
cp "$ROOT/tests/fake_upstream.cpp" "$TMP/fake.cpp"
python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/fake.cpp" --extension-dir "$TMP/extensions" >/dev/null
g++ -std=c++11 -O2 "$TMP/fake.cpp" -o "$TMP/zpaqfranz"
cd "$TMP"
# No-argument executable must show OEC quick help instead of falling through to upstream help.
./zpaqfranz > noargs.txt
grep -Fq 'OEC (Optimize + Error Correction)' noargs.txt
grep -Fq 'oecinit | oec_init' noargs.txt
# Both init spellings are accepted.
# Build two OEC parts using the new public command.
./zpaqfranz oec_a compress dummy-source --ec-data 16 --ec-stripes 8 >/dev/null
./zpaqfranz oec_a compress dummy-source --ec-data 16 --ec-stripes 8 >/dev/null
: > native_calls.log
# Build the mmap cache explicitly, then default metadata commands MUST be served
# from .idx without invoking native l/i at all.
./zpaqfranz oec_idx build compress >/dev/null
: > native_calls.log
./zpaqfranz oec_l compress >/dev/null
./zpaqfranz oec_i compress >/dev/null
[ ! -s native_calls.log ] || { echo 'default metadata OEC command did not use idx'; cat native_calls.log; exit 1; }
# Option-rich metadata keeps exact upstream semantics and uses only .000.
./zpaqfranz oec_l compress -all >/dev/null
grep -Fxq 'l|compress.000|-all' native_calls.log
if grep -E '^(l|i)\|compress\.\?\?\?' native_calls.log >/dev/null; then
  echo 'metadata OEC command touched multipart pattern'; exit 1
fi
./zpaqfranz oec_idx verify compress >/dev/null
# Extraction commands retain native payload semantics and address the parts.
./zpaqfranz oec_x compress file.txt -to out >/dev/null
./zpaqfranz oec_e compress file.txt >/dev/null
grep -Fxq 'x|compress.???|file.txt|-to|out' native_calls.log
grep -Fxq 'e|compress.???|file.txt' native_calls.log
# Generic pattern/index inference.
rm -f legacy_*.zpaq legacy_*.zpaq.ec
for n in 1 2; do cp compress.001 "$(printf 'legacy_%04d.zpaq' "$n")"; done
./zpaqfranz oec_init 'legacy_????.zpaq' --ec-data 16 --ec-stripes 8 >/dev/null
: > native_calls.log
./zpaqfranz oec_l 'legacy_????.zpaq' >/dev/null
./zpaqfranz oec_x 'legacy_????.zpaq' foo >/dev/null
# oec_init built legacy_0000.zpaq.idx, so l is mmap-served; extraction still uses payload pattern.
if grep -E '^l\|' native_calls.log >/dev/null; then echo 'legacy oec_l unexpectedly bypassed idx'; exit 1; fi
grep -Fxq 'x|legacy_????.zpaq|foo' native_calls.log
# Custom zero-part location applies to metadata and acts as OEC authority for extraction.
cp compress.000 custom.zero
./zpaqfranz oec_l compress --oec-index custom.zero >/dev/null
./zpaqfranz oec_x compress --oec-index custom.zero foo >/dev/null
# Old public names are intentionally no longer extension commands.
if ./zpaqfranz trunkadd compress dummy-source >/dev/null 2>&1; then echo 'old trunkadd alias unexpectedly handled'; exit 1; fi
if ./zpaqfranz trunkinit 'compress.???' >/dev/null 2>&1; then echo 'old trunkinit alias unexpectedly handled'; exit 1; fi
# State format migrates to OECST1.
head -n1 compress.ecstate | grep -Fxq 'OECST1'
echo 'OEC COMMAND TESTS PASS'
