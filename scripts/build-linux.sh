#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM="${1:-$ROOT/upstream/zpaqfranz.cpp}"
OUT="${2:-$ROOT/build/zpaqoec}"

if [[ ! -f "$UPSTREAM" ]]; then
  printf 'upstream source not found: %s\n' "$UPSTREAM" >&2
  exit 2
fi
UPSTREAM="$(realpath "$UPSTREAM")"
UPSTREAM_DIR="$(dirname "$UPSTREAM")"

mkdir -p "$(dirname "$OUT")"
python3 "$ROOT/scripts/apply_to_upstream.py" "$UPSTREAM"

EXT_HEADER="$UPSTREAM_DIR/extensions/zpaqfranz_ext.hpp"
if [[ ! -f "$EXT_HEADER" ]]; then
  printf 'extension header missing after patch: %s\n' "$EXT_HEADER" >&2
  exit 3
fi

g++ -O3 -std=c++11 -I"$UPSTREAM_DIR" "$UPSTREAM" -o "$OUT" -pthread
printf 'built: %s\n' "$OUT"
