#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$ROOT/tests/oecinit-test-run"
rm -rf "$TMP"; mkdir -p "$TMP/extensions"
cp "$ROOT/tests/fake_upstream.cpp" "$TMP/fake.cpp"
python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/fake.cpp" --extension-dir "$TMP/extensions" >/dev/null
g++ -std=c++11 -O2 "$TMP/fake.cpp" -o "$TMP/zpaqfranz"
cd "$TMP"
# Make 3 legacy parts without an index, simulating a pre-existing multipart archive.
for n in 1 2 3; do
  p=$(printf 'compress.%03d' "$n")
  python3 - "$p" "$n" <<'PY'
import sys
p=sys.argv[1]; n=int(sys.argv[2])
with open(p,'wb') as f:
    f.write(bytes(((i*13+n*7)&255) for i in range(1024*1024+123*n)))
PY
done
./zpaqfranz oecinit 'compress.???' --ec-data 16 --ec-stripes 8 >/dev/null
[ -f compress.000 ] && [ -f compress.000.ec ]
for n in 1 2 3; do p=$(printf 'compress.%03d' "$n"); [ -f "$p.ec" ]; ./zpaqfranz ec verify "$p" >/dev/null; done
./zpaqfranz ec verify compress.000 >/dev/null
grep -q 'last_part=3' compress.ecstate
# Refuse accidental overwrite.
if ./zpaqfranz oecinit 'compress.???' >/dev/null 2>&1; then echo 'expected refusal without --force'; exit 1; fi
# Force rebuild is transactional and recreates all EC.
./zpaqfranz oecinit 'compress.???' --force --ec-data 16 --ec-stripes 8 >/dev/null
./zpaqfranz ec verify compress.000 >/dev/null
# Generic .zpaq naming/index inference.
rm -f legacy_*.zpaq legacy_*.zpaq.ec
for n in 1 2; do cp compress.001 "$(printf 'legacy_%04d.zpaq' "$n")"; done
./zpaqfranz oecinit 'legacy_????.zpaq' --ec-data 16 --ec-stripes 8 >/dev/null
[ -f legacy_0000.zpaq ] && [ -f legacy_0000.zpaq.ec ]
[ -f legacy_0001.zpaq.ec ] && [ -f legacy_0002.zpaq.ec ]
echo 'OECINIT TESTS PASS'
