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
