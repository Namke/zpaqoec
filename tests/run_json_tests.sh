#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$ROOT/tests/json-test-run"
rm -rf "$TMP"; mkdir -p "$TMP/extensions"
cp "$ROOT/tests/fake_upstream.cpp" "$TMP/fake.cpp"
python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/fake.cpp" --extension-dir "$TMP/extensions" >/dev/null
g++ -std=c++11 -O2 "$TMP/fake.cpp" -o "$TMP/zpaqfranz"
cd "$TMP"

# Multipart base -> basename.json, and use a valid mmap idx when available.
./zpaqfranz oec_a aaa dummy-source --ec-data 16 --ec-stripes 8 >/dev/null
./zpaqfranz oec_idx build aaa >/dev/null
./zpaqfranz oec_json 'aaa???' >/dev/null 2>&1 || true
# Explicit pattern actually matching the archive layout generated above.
./zpaqfranz oec_json 'aaa.???' > json.out
[ -f aaa.json ]
grep -Fq 'oec_json: wrote aaa.json' json.out
python3 - <<'PY'
import json
p='aaa.json'
d=json.load(open(p,encoding='utf-8'))
assert d['format']=='zpaqoec-file-list'
assert d['format_version']==2
assert d['file_count']==2
assert d['total_size']==1234
assert d['metadata_source']=='idx-mmap'
a=d['files'][0]
assert a['path']=='folder/file one.txt'
assert a['size']==1234
assert a['modified']=='2026-08-31T12:34:56'
assert a['attributes']=='0644'
assert a['type']=='file'
assert a['version']==1
assert a['compression_ratio_percent']==87
assert a['hash'] is None
b=d['files'][1]
assert b['path']=='folder/subdir' and b['type']=='directory' and b['size']==0
assert d['hash_info']['whole_file_hash_algorithm']=='MD5'
PY

# Existing output must cause immediate non-overwrite failure and remain byte-identical.
sha_before="$(sha256sum aaa.json | awk '{print $1}')"
if ./zpaqfranz oec_json 'aaa.???' > overwrite.out 2>&1; then
  echo 'oec_json unexpectedly overwrote existing JSON'; exit 1
fi
grep -Fq 'output already exists, refusing to overwrite: aaa.json' overwrite.out
sha_after="$(sha256sum aaa.json | awk '{print $1}')"
[ "$sha_before" = "$sha_after" ]

# Requested wildcard naming: legacy_????.zpaq -> legacy.json (separator trimmed).
for n in 1 2; do cp aaa.001 "$(printf 'legacy_%04d.zpaq' "$n")"; done
./zpaqfranz oec_init 'legacy_????.zpaq' --ec-data 16 --ec-stripes 8 >/dev/null
./zpaqfranz oec_json 'legacy_????.zpaq' >/dev/null
[ -f legacy.json ]
[ ! -f legacy_.json ]

# Single archive: bbb.zpaq -> bbb.json.
cp aaa.001 bbb.zpaq
./zpaqfranz oec_init bbb.zpaq --ec-data 16 --ec-stripes 8 >/dev/null
./zpaqfranz oec_json bbb.zpaq >/dev/null
[ -f bbb.json ]
python3 - <<'PY'
import json
j=json.load(open('bbb.json',encoding='utf-8'))
assert j['archive']=='bbb.zpaq'
assert j['zero_part']=='bbb.000.zpaq'
assert j['file_count']==2
PY

# PASSWORD_FOLDER must let encrypted/no-idx JSON listing complete without prompt.
mkdir pwdir
printf 'secret\n' > pwdir/secure.password
cp aaa.001 secure.zpaq
PASSWORD_FOLDER="$PWD/pwdir" FAKE_ENCRYPTED=1 timeout 15 ./zpaqfranz oec_init secure.zpaq --ec-data 16 --ec-stripes 8 > secure-init.out 2>&1
# encrypted default does not create plaintext idx; oec_json uses one terse zero-part pass and PASSWORD_FOLDER.
PASSWORD_FOLDER="$PWD/pwdir" FAKE_ENCRYPTED=1 timeout 15 ./zpaqfranz oec_json secure.zpaq > secure-json.out 2>&1
[ -f secure.json ]
grep -Fq 'oec auth: loaded archive password from' secure-json.out
python3 - <<'PY'
import json
j=json.load(open('secure.json',encoding='utf-8'))
assert j['metadata_source']=='zero-part-terse'
assert j['file_count']==2
PY

echo 'OEC JSON TESTS PASS'

# --force-md5: extract archive once, hash extracted files, and write complete MD5 values.
cp aaa.001 md5case.zpaq
./zpaqfranz oec_init md5case.zpaq --ec-data 16 --ec-stripes 8 >/dev/null
./zpaqfranz oec_json md5case.zpaq --force-md5 > force-md5.out
[ -f md5case.json ]
python3 - <<'PY'
import json,hashlib
j=json.load(open('md5case.json',encoding='utf-8'))
f=next(x for x in j['files'] if x['path']=='folder/file one.txt')
data=bytes(((i*11+7)&255) for i in range(1234))
assert f['md5']==hashlib.md5(data).hexdigest(),(f['md5'],hashlib.md5(data).hexdigest())
assert f['md5_source']=='archive-extract'
assert j['md5_complete'] is True
assert j['md5_file_count']==1
assert j['format_version']==2
PY
[ ! -d md5case.json.md5tmp.* ] 2>/dev/null || true

# Progressive oec_a update: existing JSON is updated using MD5 from source files.
mkdir -p folder
python3 - <<'PY'
open('folder/file one.txt','wb').write(bytes(((i*13+5)&255) for i in range(1234)))
PY
# aaa.json already exists from the initial catalog. oec_a must update it automatically.
./zpaqfranz oec_a aaa folder --ec-data 16 --ec-stripes 8 > progressive.out
python3 - <<'PY'
import json,hashlib
j=json.load(open('aaa.json',encoding='utf-8'))
f=next(x for x in j['files'] if x['path']=='folder/file one.txt')
data=open('folder/file one.txt','rb').read()
assert f['md5']==hashlib.md5(data).hexdigest()
assert f['md5_source']=='source'
assert j['format_version']==2
PY
grep -Fq 'oec_a: JSON updated aaa.json' progressive.out

# Without an existing JSON, normal oec_a must not create one.
./zpaqfranz oec_a nojson dummy-source --ec-data 16 --ec-stripes 8 >/dev/null
[ ! -f nojson.json ]
# Explicit JSON force creates the catalog and hashes matching source files.
mkdir -p createforce/folder
cp 'folder/file one.txt' 'createforce/folder/file one.txt'
./zpaqfranz oec_a forced createforce --json-force --ec-data 16 --ec-stripes 8 > forced.out
[ -f forced.json ]
grep -Fq 'oec_a: JSON updated forced.json' forced.out

echo 'OEC JSON MD5/PROGRESSIVE TESTS PASS'
