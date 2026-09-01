#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$ROOT/tests/oec-ignore-test-run"
rm -rf "$TMP"; mkdir -p "$TMP/extensions" "$TMP/src/build" "$TMP/src/logs/a"
cp "$ROOT/tests/fake_upstream.cpp" "$TMP/fake.cpp"
python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/fake.cpp" --extension-dir "$TMP/extensions" >/dev/null
g++ -std=c++11 -O2 "$TMP/fake.cpp" -o "$TMP/zpaqoec"
cd "$TMP"
printf x > src/keep.txt
printf x > src/junk.tmp
printf x > src/important.tmp
printf x > src/build/a.bin
printf x > src/logs/a/x.log
printf x > src/logs/a/x.txt
printf x > src/skip.bak
printf x > src/keep.bak
printf x > src/skip.cache
printf x > src/keep.cache
mkdir -p src/nested
printf x > src/rootonly.dat
printf x > src/nested/rootonly.dat
cat > src/zpaq.ignore <<'EOF'
# OEC ignore
*.tmp
!important.tmp
build/
logs/**/*.log
!keep.cache
/rootonly.dat
EOF
cat > src/.gitignore <<'EOF'
*.bak
!keep.bak
*.cache
EOF

# zpaq.ignore is automatic; .gitignore must NOT apply without -gitignore.
./zpaqoec oec_a compress src --ec-data 16 --ec-stripes 8 > run1.txt
grep -q 'ignore rules loaded from 1 file(s)' run1.txt
grep -Fxq 'src/junk.tmp' fake_exclude_seen.txt
grep -Fxq 'src/build' fake_exclude_seen.txt
grep -Fxq 'src/logs/a/x.log' fake_exclude_seen.txt
grep -Fxq 'src/rootonly.dat' fake_exclude_seen.txt
! grep -Fxq 'src/nested/rootonly.dat' fake_exclude_seen.txt
! grep -Fxq 'src/important.tmp' fake_exclude_seen.txt
! grep -Fxq 'src/keep.txt' fake_exclude_seen.txt
! grep -Fxq 'src/skip.bak' fake_exclude_seen.txt

# -gitignore merges .gitignore first, then zpaq.ignore so OEC-specific rules can override.
./zpaqoec oec_a compress src -gitignore --ec-data 16 --ec-stripes 8 > run2.txt
grep -q 'ignore rules loaded from 2 file(s)' run2.txt
grep -Fxq 'src/skip.bak' fake_exclude_seen.txt
! grep -Fxq 'src/keep.bak' fake_exclude_seen.txt
grep -Fxq 'src/skip.cache' fake_exclude_seen.txt
! grep -Fxq 'src/keep.cache' fake_exclude_seen.txt
# OEC-only switch must never reach native add.
! grep -q -- '-gitignore' native_calls.log
# Temporary native exclusion list is removed after add.
last_exclude=$(tail -1 native_calls.log | tr '|' '\n' | awk 'p{print;exit} $0=="-exclude"{p=1}')
[ -n "$last_exclude" ] && [ ! -e "$last_exclude" ]

# Progressive JSON source hashing must use the same ignore set.
mkdir -p jsonsrc/folder
printf payload > 'jsonsrc/folder/file one.txt'
printf 'folder/file one.txt\n' > jsonsrc/zpaq.ignore
./zpaqoec oec_a jsoncase jsonsrc --json-force --ec-data 16 --ec-stripes 8 > run3.txt
grep -q 'source MD5 collected for .* non-ignored files' run3.txt
python3 - <<'PY2'
import json
with open('jsoncase.json','r',encoding='utf-8') as f: d=json.load(f)
r={x['path']:x for x in d['files']}['folder/file one.txt']
assert r.get('md5') in (None,''), r
PY2

echo 'OEC IGNORE TESTS PASS'
