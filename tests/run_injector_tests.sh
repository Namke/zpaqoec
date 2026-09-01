#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$ROOT/tests/injector-test-run"
rm -rf "$TMP"; mkdir -p "$TMP"
cp "$ROOT/tests/fake_upstream.cpp" "$TMP/fake.cpp"
# Simulate a source patched by <=0.1.5, where the extension was line 1.
printf '#include "extensions/zpaqfranz_ext.hpp"\n' | cat - "$TMP/fake.cpp" > "$TMP/legacy.cpp"
mv "$TMP/legacy.cpp" "$TMP/fake.cpp"
python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/fake.cpp" >/dev/null
# Simulate a source whose dispatcher marker was written by 0.1.x, then migrate it.
sed -i 's/ZPAQFRANZ_OEC_DISPATCH/ZPAQFRANZ_TRUNKEC_DISPATCH/' "$TMP/fake.cpp"
python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/fake.cpp" >/dev/null
grep -q 'ZPAQFRANZ_OEC_DISPATCH' "$TMP/fake.cpp"
! grep -q 'ZPAQFRANZ_TRUNKEC_DISPATCH' "$TMP/fake.cpp"
[[ $(grep -c 'zfext::dispatch(argc, argv)' "$TMP/fake.cpp") -eq 1 ]]
python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/fake.cpp" | grep -q 'already patched'
count=$(grep -c '^#include "extensions/zpaqfranz_ext.hpp"$' "$TMP/fake.cpp")
inc=$(grep -n -m1 '^#include "extensions/zpaqfranz_ext.hpp"$' "$TMP/fake.cpp" | cut -d: -f1)
entry=$(grep -n -m1 -E '\bint[[:space:]]+zpaq_main_internal[[:space:]]*\(' "$TMP/fake.cpp" | cut -d: -f1)
[[ "$count" -eq 1 ]]
[[ "$inc" -lt "$entry" ]]
[[ "$inc" -gt 1 ]]
# The dispatcher must be inside the common internal entry, not the decoy/outer main.
hook=$(grep -n -m1 'ZPAQFRANZ_OEC_DISPATCH' "$TMP/fake.cpp" | cut -d: -f1)
[[ "$hook" -gt "$entry" ]]
g++ -std=c++11 -O2 "$TMP/fake.cpp" -o "$TMP/zpaqfranz"
echo "INJECTOR TESTS PASS"
