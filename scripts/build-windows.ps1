param(
  [string]$Upstream = "$PSScriptRoot\..\upstream\zpaqfranz.cpp",
  [string]$Out = "$PSScriptRoot\..\build\zpaqfranz-trunkec.exe"
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path "$PSScriptRoot\..").Path

if (-not (Test-Path -LiteralPath $Upstream)) {
  throw "upstream source not found: $Upstream"
}
$UpstreamPath = (Resolve-Path -LiteralPath $Upstream).Path
$UpstreamDir = Split-Path -Parent $UpstreamPath

python "$Root\scripts\apply_to_upstream.py" "$UpstreamPath"
if ($LASTEXITCODE -ne 0) { throw "apply_to_upstream.py failed with exit code $LASTEXITCODE" }

$ExtHeader = Join-Path $UpstreamDir "extensions\zpaqfranz_ext.hpp"
if (-not (Test-Path -LiteralPath $ExtHeader)) {
  throw "extension header missing after patch: $ExtHeader"
}

$OutDir = Split-Path -Parent $Out
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# MSYS2/MinGW g++ is the most compatible path with upstream zpaqfranz build instructions.
# Explicitly add the upstream source directory because the injected include is:
#   #include "extensions/zpaqfranz_ext.hpp"
# Do not rely on compiler-specific quoted-include search behavior for an absolute source path.
& g++ -O3 -std=c++11 "-I$UpstreamDir" "$UpstreamPath" -o "$Out" -pthread -static
if ($LASTEXITCODE -ne 0) { throw "g++ failed with exit code $LASTEXITCODE" }
Write-Host "built: $Out"
