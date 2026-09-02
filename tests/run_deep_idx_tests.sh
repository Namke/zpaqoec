#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$ROOT/tests/deep-test-run"; rm -rf "$TMP"; mkdir -p "$TMP"
g++ -std=c++11 -O2 -I"$ROOT/src" "$ROOT/tests/deep_idx_test.cpp" -o "$TMP/deep"
(cd "$TMP" && ./deep)
