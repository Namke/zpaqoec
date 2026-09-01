#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$ROOT/tests/injector-test-run"
rm -rf "$TMP"; mkdir -p "$TMP"
cp "$ROOT/tests/fake_upstream.cpp" "$TMP/fake.cpp"

# Simulate old overlays: line-1 include and old marker. The new injector must
# remove/relocate them and add the OEC bridge safely.
printf '#include "extensions/zpaqfranz_ext.hpp"\n' | cat - "$TMP/fake.cpp" > "$TMP/legacy.cpp"
mv "$TMP/legacy.cpp" "$TMP/fake.cpp"
python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/fake.cpp" >/dev/null
sed -i '0,/ZPAQFRANZ_OEC_DISPATCH/s//ZPAQFRANZ_TRUNKEC_DISPATCH/' "$TMP/fake.cpp"
python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/fake.cpp" >/dev/null

! grep -q 'ZPAQFRANZ_TRUNKEC_DISPATCH' "$TMP/fake.cpp"
[[ $(grep -c '^#include "extensions/zpaqfranz_ext.hpp"$' "$TMP/fake.cpp") -eq 1 ]]
[[ $(grep -c 'ZPAQOEC_BRIDGE_DECL' "$TMP/fake.cpp") -eq 1 ]]
[[ $(grep -c 'ZPAQOEC_BRIDGE_DEF' "$TMP/fake.cpp") -eq 1 ]]
# fake_upstream has one no-arg main (must be skipped), two parameterized mains,
# and two argc/argv internal entries. Exactly four eligible entries get hooks.
[[ $(grep -c 'ZPAQFRANZ_OEC_DISPATCH' "$TMP/fake.cpp") -eq 4 ]]
# The no-arg main body must remain free of argc/argv bridge references.
python3 - "$TMP/fake.cpp" <<'PY2'
import pathlib, re, sys
t=pathlib.Path(sys.argv[1]).read_text()
m=re.search(r'int\s+main\s*\(\s*\)\s*\{([^}]*)\}', t, re.S)
assert m, 'no-arg main decoy missing'
assert 'zfext_oec_dispatch_bridge' not in m.group(1), 'no-arg main was incorrectly instrumented'
PY2

# Repatch must not accumulate declarations/includes/hooks.
python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/fake.cpp" >/dev/null
[[ $(grep -c '^#include "extensions/zpaqfranz_ext.hpp"$' "$TMP/fake.cpp") -eq 1 ]]
[[ $(grep -c 'ZPAQOEC_BRIDGE_DECL' "$TMP/fake.cpp") -eq 1 ]]
[[ $(grep -c 'ZPAQOEC_BRIDGE_DEF' "$TMP/fake.cpp") -eq 1 ]]
[[ $(grep -c 'ZPAQFRANZ_OEC_DISPATCH' "$TMP/fake.cpp") -eq 4 ]]

g++ -std=c++11 -O2 "$TMP/fake.cpp" -o "$TMP/zpaqoec"
"$TMP/zpaqoec" oec_h | grep -Fq 'OEC (Optimize + Error Correction)'
"$TMP/zpaqoec" oec_version | grep -Fq 'zpaqoec OEC overlay 0.3.4'
"$TMP/zpaqoec" | grep -Fq 'OEC (Optimize + Error Correction)'
echo "INJECTOR TESTS PASS"
