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
Z="$ROOT/src/zfec.hpp"
grep -Fq '#include <windows.h>' "$H"
grep -Fq 'FindFirstFileW' "$H"
grep -Fq 'FindNextFileW' "$H"
grep -Fq 'CreateProcessW' "$H"
grep -Fq 'oec_windows_utf8_argv' "$H"
! grep -Eq '_findfirst\s*\(' "$H"
! grep -Eq '_findnext\s*\(' "$H"
! grep -Eq '_spawnv\s*\(' "$H"
grep -Fq 'fopen_utf8' "$Z"
grep -Fq '_wfopen' "$Z"
grep -Fq '_wstat64' "$Z"
grep -Fq 'MoveFileExW' "$Z"
echo "WINDOWS UTF-8 FILESYSTEM STATIC TESTS PASS"

grep -Fq 'EOC_TEMP' "$ROOT/src/zpaqfranz_ext.hpp"

grep -Fq -- "-SimpleMatch 'OecHybridHTIndex' -Quiet" "$P"
grep -Fq "grep -Fq 'OecHybridHTIndex'" "$ROOT/scripts/build-linux.sh"
grep -Fq 'deep scan diagnostics:' "$ROOT/scripts/apply_to_upstream.py"
