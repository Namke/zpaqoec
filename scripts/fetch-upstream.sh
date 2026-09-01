#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/upstream"
URL="https://raw.githubusercontent.com/fcorbelli/zpaqfranz/64.8/zpaqfranz.cpp"
curl -fL "$URL" -o "$ROOT/upstream/zpaqfranz.cpp"
echo "downloaded zpaqfranz 64.8 -> $ROOT/upstream/zpaqfranz.cpp"
