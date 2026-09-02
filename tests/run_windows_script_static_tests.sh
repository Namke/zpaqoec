#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
P="$ROOT/scripts/build-windows.ps1"
# PowerShell automatic variable $args is case-insensitive. Never use Args as a
# declared parameter in the smoke helper, or command arrays can disappear.
! grep -Eq 'Assert-OecOutput\([^)]*\$Args([, )]|$)' "$P"
grep -Fq '[string[]]$CommandArgs' "$P"
grep -Fq '& $BuiltExe @CommandArgs' "$P"
grep -Fq -- "-CommandArgs @('oec_version')" "$P"
grep -Fq -- "-CommandArgs @('oecinit')" "$P"
grep -Fq -- "-ExpectedExit 2" "$P"
echo "WINDOWS BUILD SCRIPT STATIC TESTS PASS"
H="$ROOT/src/zpaqfranz_ext.hpp"
grep -Fq '#include <io.h>' "$H"
grep -Fq '#include <sys/stat.h>' "$H"
grep -Fq 'struct _finddata_t fd' "$H"
! grep -Fq '_finddata64_t' "$H"
! grep -Fq '_findfirst64' "$H"
! grep -Fq '_findnext64' "$H"
echo "WINDOWS CRT ENUMERATION STATIC TESTS PASS"

grep -Fq 'EOC_TEMP' "$ROOT/src/zpaqfranz_ext.hpp"

grep -Fq -- "-SimpleMatch 'OecHybridHTIndex' -Quiet" "$P"
grep -Fq "grep -Fq 'OecHybridHTIndex'" "$ROOT/scripts/build-linux.sh"
grep -Fq 'deep scan diagnostics:' "$ROOT/scripts/apply_to_upstream.py"
