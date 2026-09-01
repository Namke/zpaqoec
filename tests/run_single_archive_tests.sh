#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$ROOT/tests/single-archive-test-run"
rm -rf "$TMP"; mkdir -p "$TMP/extensions"
cp "$ROOT/tests/fake_upstream.cpp" "$TMP/fake.cpp"
python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/fake.cpp" --extension-dir "$TMP/extensions" >/dev/null
g++ -std=c++11 -O2 "$TMP/fake.cpp" -o "$TMP/zpaqfranz"
cd "$TMP"

# Legacy single-file archive: no .001 part exists.
python3 - <<'PY'
with open('single.zpaq','wb') as f:
    f.write(bytes((i*23+11)&255 for i in range(2*1024*1024+333)))
PY
orig_size=$(wc -c < single.zpaq)
./zpaqfranz oec_init single.zpaq --ec-data 16 --ec-stripes 8 > init.out

grep -Fq 'mode=single' init.out
[ -f single.zpaq ]
[ -f single.zpaq.ec ]
[ -f single.000.zpaq ]
[ -f single.000.zpaq.ec ]
[ -f single.idx ]
[ ! -e single.zpaq.001 ]
[ ! -e single.ecstate ]
[ "$(wc -c < single.zpaq)" -eq "$orig_size" ]
./zpaqfranz ec verify single.zpaq >/dev/null
./zpaqfranz ec verify single.000.zpaq >/dev/null
./zpaqfranz oec_idx verify single.zpaq >/dev/null

# Metadata defaults are served from the single archive's mmap cache.
: > native_calls.log
./zpaqfranz oec_l single.zpaq >/dev/null
./zpaqfranz oec_i single.zpaq >/dev/null
[ ! -s native_calls.log ] || { echo 'single oec_l/i unexpectedly invoked native parser'; cat native_calls.log; exit 1; }
# Option-rich metadata uses only the zero index.
./zpaqfranz oec_l single.zpaq -all >/dev/null
grep -Fxq 'l|single.000.zpaq|-all' native_calls.log
# Extraction reads the exact single payload, not a synthesized .??? pattern.
./zpaqfranz oec_x single.zpaq file.txt -to out >/dev/null
./zpaqfranz oec_e single.zpaq file.txt >/dev/null
grep -Fxq 'x|single.zpaq|file.txt|-to|out' native_calls.log
grep -Fxq 'e|single.zpaq|file.txt' native_calls.log

# Single-file add appends to the same archive, uses .000, and regenerates EC.
before_add=$(wc -c < single.zpaq)
./zpaqfranz oec_a single.zpaq dummy-source --ec-data 16 --ec-stripes 8 --idx-refresh >/dev/null
after_add=$(wc -c < single.zpaq)
[ "$after_add" -gt "$before_add" ]
[ ! -e single.zpaq.001 ]
./zpaqfranz ec verify single.zpaq >/dev/null
./zpaqfranz ec verify single.000.zpaq >/dev/null
./zpaqfranz oec_idx verify single.zpaq >/dev/null
grep -Fq 'a|single.zpaq|dummy-source|-index|single.000.zpaq' native_calls.log

# Minimum guarantee: even if native zero-index creation fails, data EC survives.
python3 - <<'PY'
with open('failindex.zpaq','wb') as f:
    f.write(bytes((i*29+3)&255 for i in range(1024*1024+99)))
PY
set +e
./zpaqfranz oecinit failindex.zpaq --ec-data 16 --ec-stripes 8 > fail.out 2>&1
rc=$?
set -e
[ "$rc" -ne 0 ]
[ -f failindex.zpaq.ec ]
[ ! -e failindex.000.zpaq ]
grep -Fq 'single archive EC is ready, but zero-part index rebuild failed' fail.out
./zpaqfranz ec verify failindex.zpaq >/dev/null

# Encrypted zero-part policy: do not leak metadata to plaintext IDX by default.
python3 - <<'PY'
with open('encrypted.zpaq','wb') as f:
    f.write(bytes((i*31+9)&255 for i in range(1024*1024+17)))
PY
printf 'secret\n' | FAKE_ENCRYPTED=1 ./zpaqfranz oecinit encrypted.zpaq --ec-data 16 --ec-stripes 8 > encrypted.out 2>&1
grep -Fq 'plaintext idx cache is disabled by default' encrypted.out
[ -f encrypted.zpaq.ec ]
[ -f encrypted.000.zpaq ]
[ -f encrypted.000.zpaq.ec ]
[ ! -e encrypted.idx ]
grep -Fq 'idx=skipped-encrypted-plaintext-policy' encrypted.out

# Explicit plaintext opt-in: prompts are visible by stage and auth chatter is not cached.
python3 - <<'PY'
with open('encrypted2.zpaq','wb') as f:
    f.write(bytes((i*37+5)&255 for i in range(1024*1024+23)))
PY
python3 - <<'PYEXPECT'
import os, pexpect
env=os.environ.copy(); env['FAKE_ENCRYPTED']='1'
with open('encrypted2.out','w',encoding='utf-8') as log:
    p=pexpect.spawn('./zpaqfranz',['oecinit','encrypted2.zpaq','--idx-plaintext','--ec-data','16','--ec-stripes','8'],env=env,encoding='utf-8',timeout=30)
    p.logfile=log
    p.expect('Enter AES password:'); p.sendline('secret')
    p.expect('stage 1/2 native l requires an archive password'); p.sendline('secret')
    p.expect('stage 2/2 native i requires an archive password'); p.sendline('secret')
    p.expect(pexpect.EOF)
    if p.exitstatus not in (0,None): raise SystemExit(p.exitstatus)
PYEXPECT
grep -Fq 'oec idx: stage 1/2 list metadata' encrypted2.out
grep -Fq 'stage 1/2 native l requires an archive password' encrypted2.out
grep -Fq 'oec idx: stage 2/2 version/info metadata' encrypted2.out
grep -Fq 'stage 2/2 native i requires an archive password' encrypted2.out
[ -f encrypted2.idx ]
./zpaqfranz oec_idx verify encrypted2.zpaq >/dev/null
# Cached list must not replay an auth prompt.
FAKE_ENCRYPTED=1 ./zpaqfranz oec_l encrypted2.zpaq --idx-plaintext > encrypted2-list.out
grep -Fq 'fake l metadata' encrypted2-list.out
! grep -Fq 'Enter AES password' encrypted2-list.out


# PASSWORD_FOLDER: when no explicit -key/FRANZKEY is present, load the first
# plaintext line from <normalized-archive-name>.password and never prompt.
mkdir -p pwdir
printf 'secret-from-file\r\nignored-second-line\n' > pwdir/pwtest.password
python3 - <<'PYDATA'
with open('pwtest.zpaq','wb') as f:
    f.write(bytes((i*41+7)&255 for i in range(512*1024+31)))
PYDATA
PASSWORD_FOLDER="$PWD/pwdir" FAKE_ENCRYPTED=1 timeout 15 ./zpaqfranz oecinit pwtest.zpaq --idx-plaintext --ec-data 16 --ec-stripes 8 > pwfolder.out 2>&1
grep -Fq 'oec auth: loaded archive password from' pwfolder.out
! grep -Fq 'Enter AES password:' pwfolder.out
[ -f pwtest.zpaq.ec ]
[ -f pwtest.000.zpaq ]
[ -f pwtest.idx ]

# Requested wildcard naming rule: test???.zpaq -> PASSWORD_FOLDER/test.password.
printf 'secret-pattern\n' > pwdir/test.password
cp single.zpaq test001.zpaq
cp single.zpaq test002.zpaq
PASSWORD_FOLDER="$PWD/pwdir" FAKE_ENCRYPTED=1 timeout 15 ./zpaqfranz oecinit 'test???.zpaq' --idx-plaintext --ec-data 16 --ec-stripes 8 > pwpatt.out 2>&1
grep -Fq 'pwdir/test.password' pwpatt.out
! grep -Fq 'Enter AES password:' pwpatt.out

# Exact native command also receives the preflight because the bridge runs
# before upstream parsing. foo.000.zpaq maps back to foo.password.
printf 'secret-zero\n' > pwdir/pwtest.password
PASSWORD_FOLDER="$PWD/pwdir" FAKE_ENCRYPTED=1 timeout 10 ./zpaqfranz l pwtest.000.zpaq > pwnative.out 2>&1
grep -Fq 'oec auth: loaded archive password from' pwnative.out
! grep -Fq 'Enter AES password:' pwnative.out

# Existing FRANZKEY has priority and suppresses PASSWORD_FOLDER lookup.
FRANZKEY=already-set PASSWORD_FOLDER="$PWD/pwdir" FAKE_ENCRYPTED=1 timeout 10 ./zpaqfranz l pwtest.000.zpaq > franzkey-priority.out 2>&1
! grep -Fq 'oec auth: loaded archive password from' franzkey-priority.out
! grep -Fq 'Enter AES password:' franzkey-priority.out

echo 'OEC SINGLE ARCHIVE TESTS PASS'
