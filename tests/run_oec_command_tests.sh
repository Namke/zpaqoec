#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$ROOT/tests/oec-command-test-run"
rm -rf "$TMP"; mkdir -p "$TMP/extensions"
cp "$ROOT/tests/fake_upstream.cpp" "$TMP/fake.cpp"
python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/fake.cpp" --extension-dir "$TMP/extensions" >/dev/null
g++ -std=c++11 -O2 "$TMP/fake.cpp" -o "$TMP/zpaqfranz"
cd "$TMP"
# Build two OEC parts using the new public command.
./zpaqfranz oec_a compress dummy-source --ec-data 16 --ec-stripes 8 >/dev/null
./zpaqfranz oec_a compress dummy-source --ec-data 16 --ec-stripes 8 >/dev/null
: > native_calls.log
# Metadata commands MUST route only to the zero part.
./zpaqfranz oec_l compress -all >/dev/null
./zpaqfranz oec_i compress >/dev/null
grep -Fxq 'l|compress.000|-all' native_calls.log
grep -Fxq 'i|compress.000' native_calls.log
if grep -E '^(l|i)\|compress\.\?\?\?' native_calls.log >/dev/null; then
  echo 'metadata OEC command touched multipart pattern'; exit 1
fi
# Extraction commands retain native payload semantics and address the parts.
./zpaqfranz oec_x compress file.txt -to out >/dev/null
./zpaqfranz oec_e compress file.txt >/dev/null
grep -Fxq 'x|compress.???|file.txt|-to|out' native_calls.log
grep -Fxq 'e|compress.???|file.txt' native_calls.log
# Generic pattern/index inference.
rm -f legacy_*.zpaq legacy_*.zpaq.ec
for n in 1 2; do cp compress.001 "$(printf 'legacy_%04d.zpaq' "$n")"; done
./zpaqfranz oecinit 'legacy_????.zpaq' --ec-data 16 --ec-stripes 8 >/dev/null
: > native_calls.log
./zpaqfranz oec_l 'legacy_????.zpaq' >/dev/null
./zpaqfranz oec_x 'legacy_????.zpaq' foo >/dev/null
grep -Fxq 'l|legacy_0000.zpaq' native_calls.log
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
