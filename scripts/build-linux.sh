#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM="${1:-$ROOT/upstream/zpaqfranz.cpp}"
OUT="${2:-$ROOT/build/zpaqfranz-trunkec}"
mkdir -p "$(dirname "$OUT")"
python3 "$ROOT/scripts/apply_to_upstream.py" "$UPSTREAM"
g++ -O3 -std=c++11 "$UPSTREAM" -o "$OUT" -pthread
printf 'built: %s\n' "$OUT"
