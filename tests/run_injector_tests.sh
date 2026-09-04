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
[ -f "$TMP/extensions/oec_md5.hpp" ]
[ -f "$TMP/extensions/oec_cold.hpp" ]
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

# Regression for zpaqfranz signature/constructor drift: deep patching must not
# depend on exact `int Jidac::add()` spelling or the historical expected-size
# expression.  Keep the native constructor arguments byte-for-byte.
cp "$ROOT/tests/fake_upstream.cpp" "$TMP/drift.cpp"
python3 - "$TMP/drift.cpp" <<'PYDRIFT'
import pathlib,sys
p=pathlib.Path(sys.argv[1]); t=p.read_text()
t=t.replace('int add();', 'int add(bool compat);')
t=t.replace('int Jidac::add() {', 'int Jidac::add(bool compat) {')
t=t.replace('HTIndex htinv(ht, ht.size()+(total_size>>(10+fragment))+vf.size());',
            'HTIndex   htinv ( ht, unsigned(ht.size() + vf.size() + 17) );')
t=t.replace('return errors;', 'return compat ? errors : errors;', 1)
p.write_text(t)
PYDRIFT
OUT=$(python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/drift.cpp")
echo "$OUT" | grep -Fq 'OEC deep Jidac hook: 1'
grep -Eq 'OecHybridHTIndex[[:space:]]+htinv[[:space:]]*\([[:space:]]*ht,[[:space:]]*unsigned\(ht\.size\(\)[[:space:]]*\+[[:space:]]*vf\.size\(\)[[:space:]]*\+[[:space:]]*17\)[[:space:]]*\)' "$TMP/drift.cpp"
grep -Fq 'ZPAQOEC_DEEP_COMMIT' "$TMP/drift.cpp"
# Re-patching the drift form remains idempotent.
OUT=$(python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/drift.cpp")
echo "$OUT" | grep -Fq 'OEC deep Jidac hook: 1'
[[ $(grep -c 'ZPAQOEC_DEEP_COMMIT' "$TMP/drift.cpp") -eq 1 ]]

# 64.8-style aggressive formatting drift: the method name and return type may
# span lines, and the native inverse-index local is not required to be named
# htinv. The injector must preserve the entire constructor expression.
cp "$ROOT/tests/fake_upstream.cpp" "$TMP/multiline.cpp"
python3 - "$TMP/multiline.cpp" <<'PYMULTI'
import pathlib,re,sys
p=pathlib.Path(sys.argv[1]); t=p.read_text()
# Start from a native shape; the fixture itself may already contain an older OEC patch.
t=t.replace('OecHybridHTIndex','HTIndex')
t=re.sub(r'(?m)^\s*#include\s+"extensions/oec_deep\.hpp"\s*$', '', t)
t=re.sub(r'(?m)^\s*/\* ZPAQOEC_DEEP_COMMIT \*/\s*htinv\.commit\(\);\s*$', '', t)
t=t.replace('int Jidac::add() {', 'int\nJidac\n  ::\nadd\n(\n)\n{', 1)
t=t.replace('HTIndex htinv(ht, ht.size()+(total_size>>(10+fragment))+vf.size());',
            'HTIndex   fragment_index /*keep*/ ( ht, ht.size() + (total_size >> (10 + fragment)) + vf.size() );',1)
t=t.replace('htinv.find(', 'fragment_index.find(',1)
t=t.replace('htinv.update()', 'fragment_index.update()',1)
p.write_text(t)
PYMULTI
OUT=$(python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/multiline.cpp")
echo "$OUT" | grep -Fq 'OEC deep Jidac hook: 1'
grep -Fq 'OecHybridHTIndex   fragment_index /*keep*/ ( ht, ht.size() + (total_size >> (10 + fragment)) + vf.size() );' "$TMP/multiline.cpp"
grep -Fq 'fragment_index.commit();' "$TMP/multiline.cpp"
g++ -std=c++11 -O2 "$TMP/multiline.cpp" -o "$TMP/multiline"

g++ -std=c++11 -O2 "$TMP/fake.cpp" -o "$TMP/zpaqoec"
"$TMP/zpaqoec" oec_h | grep -Fq 'OEC (Optimize + Error Correction)'
"$TMP/zpaqoec" oec_version | grep -Fq 'zpaqoec OEC overlay 0.5.0'
"$TMP/zpaqoec" | grep -Fq 'OEC (Optimize + Error Correction)'

# Semantic fallback: do not require the method to still be spelled Jidac::add.
# The unique inverse HTIndex object is identified by find()+update() behavior.
cp "$ROOT/tests/fake_upstream.cpp" "$TMP/semantic.cpp"
python3 - "$TMP/semantic.cpp" <<'PY2'
import pathlib,re,sys
p=pathlib.Path(sys.argv[1]); t=p.read_text()
t=t.replace('#include "extensions/oec_deep.hpp"\n','')
t=t.replace('OecHybridHTIndex','HTIndex')
t=re.sub(r'(?m)^\s*/\* ZPAQOEC_DEEP_COMMIT \*/\s*htinv\.commit\(\);\s*$', '', t)
t=t.replace('int Jidac::add() {','int renamed_add_worker() {\n  std::vector<HT> ht(1);',1)
p.write_text(t)
PY2
python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/semantic.cpp" >"$TMP/semantic.log"
grep -Fq 'OEC deep Jidac hook: 1' "$TMP/semantic.log"
grep -Eq 'OecHybridHTIndex[[:space:]]+htinv[[:space:]]*\(' "$TMP/semantic.cpp"
grep -Fq '#include "extensions/oec_deep.hpp"' "$TMP/semantic.cpp"
g++ -std=c++11 -O0 -I"$TMP" "$TMP/semantic.cpp" -o "$TMP/semantic"

echo "INJECTOR TESTS PASS"
