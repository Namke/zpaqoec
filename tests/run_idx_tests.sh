#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$ROOT/tests/idx-test-run"
rm -rf "$TMP"; mkdir -p "$TMP/extensions" "$TMP/ssd"
cp "$ROOT/tests/fake_upstream.cpp" "$TMP/fake.cpp"
python3 "$ROOT/scripts/apply_to_upstream.py" "$TMP/fake.cpp" --extension-dir "$TMP/extensions" >/dev/null
g++ -std=c++11 -O2 "$TMP/fake.cpp" -o "$TMP/zpaqoec"
cd "$TMP"

./zpaqoec oec_a compress dummy-source --no-idx --ec-data 16 --ec-stripes 8 >/dev/null
./zpaqoec oec_idx build compress --idx "$TMP/ssd/compress.idx" >/dev/null
./zpaqoec oec_idx verify compress --idx "$TMP/ssd/compress.idx" >/dev/null
./zpaqoec oec_idx info compress --idx "$TMP/ssd/compress.idx" | grep -Fq 'OECIDX v2'
cat > "$TMP/check_idx2.cpp" <<'CPP'
#include "extensions/oec_idx.hpp"
#include <cstdio>
int main(){std::string e;oecidx::Cache c;if(!c.open("ssd/compress.idx","compress.000",e)){std::fprintf(stderr,"%s\n",e.c_str());return 1;}if(!c.current()||!c.has_files()||c.file_count()!=2)return 2;uint64_t i=999;if(!c.lookup_path("folder/file one.txt",i))return 3;const oecidx::FileRecord&r=c.file_records()[i];if(r.size!=1234)return 4;return 0;}
CPP
g++ -std=c++11 -O2 "$TMP/check_idx2.cpp" -o "$TMP/check_idx2"
./check_idx2

# mmap hit: default metadata reads must not invoke upstream.
: > native_calls.log
./zpaqoec oec_l compress --idx "$TMP/ssd/compress.idx" >/dev/null
./zpaqoec oec_i compress --idx "$TMP/ssd/compress.idx" >/dev/null
[ ! -s native_calls.log ] || { echo 'idx mmap hit called native parser'; cat native_calls.log; exit 1; }

# Stale source: verification fails, normal metadata read lazily rebuilds cache.
printf 'changed\n' >> compress.000
if ./zpaqoec oec_idx verify compress --idx "$TMP/ssd/compress.idx" >/dev/null 2>&1; then
  echo 'stale idx unexpectedly verified'; exit 1
fi
: > native_calls.log
./zpaqoec oec_l compress --idx "$TMP/ssd/compress.idx" >/dev/null
grep -Fxq 'l|compress.000' native_calls.log
grep -Fxq 'i|compress.000' native_calls.log
./zpaqoec oec_idx verify compress --idx "$TMP/ssd/compress.idx" >/dev/null

# Corrupt cache: CRC/header validation rejects it and lazy rebuild self-heals.
python3 - "$TMP/ssd/compress.idx" <<'PY'
import sys
p=sys.argv[1]
with open(p,'r+b') as f:
    f.seek(-1,2); b=f.read(1); f.seek(-1,2); f.write(bytes([b[0]^0x5a]))
PY
if ./zpaqoec oec_idx verify compress --idx "$TMP/ssd/compress.idx" >/dev/null 2>&1; then
  echo 'corrupt idx unexpectedly verified'; exit 1
fi
./zpaqoec oec_i compress --idx "$TMP/ssd/compress.idx" >/dev/null
./zpaqoec oec_idx verify compress --idx "$TMP/ssd/compress.idx" >/dev/null


# Explicit IDX1 -> IDX2 migration path.
cat > "$TMP/make_v1.cpp" <<'CPP'
#include "extensions/oec_idx.hpp"
#include <cstdio>
int main(){std::string e; if(!oecidx::write_cache_v1_legacy("ssd/legacy.idx","compress.000","legacy-list\n","legacy-info\n",e)){std::fprintf(stderr,"%s\n",e.c_str());return 1;}return 0;}
CPP
g++ -std=c++11 -O2 "$TMP/make_v1.cpp" -o "$TMP/make_v1"
./make_v1
if ./zpaqoec oec_idx verify compress --idx "$TMP/ssd/legacy.idx" >/dev/null 2>&1; then
  echo 'legacy IDX1 unexpectedly reported current'; exit 1
fi
./zpaqoec oec_idx info compress --idx "$TMP/ssd/legacy.idx" | grep -Fq 'OECIDX v1'
./zpaqoec oec_idx upgrade compress --idx "$TMP/ssd/legacy.idx" >/dev/null
./zpaqoec oec_idx verify compress --idx "$TMP/ssd/legacy.idx" >/dev/null
./zpaqoec oec_idx info compress --idx "$TMP/ssd/legacy.idx" | grep -Fq 'OECIDX v2'

# ensure is no-op for current cache, self-heal for stale cache.
./zpaqoec oec_idx ensure compress --idx "$TMP/ssd/legacy.idx" >/dev/null
printf 'ensure-stale\n' >> compress.000
./zpaqoec oec_idx ensure compress --idx "$TMP/ssd/legacy.idx" >/dev/null
./zpaqoec oec_idx verify compress --idx "$TMP/ssd/legacy.idx" >/dev/null

./zpaqoec oec_idx drop compress --idx "$TMP/ssd/compress.idx" >/dev/null
[ ! -e "$TMP/ssd/compress.idx" ]
echo 'OEC IDX TESTS PASS'
