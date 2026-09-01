param(
  [string]$Upstream = "$PSScriptRoot\..\upstream\zpaqfranz.cpp",
  [string]$Out = "$PSScriptRoot\..\build\zpaqfranz-trunkec.exe"
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path "$PSScriptRoot\..").Path
python "$Root\scripts\apply_to_upstream.py" $Upstream
$OutDir = Split-Path -Parent $Out
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
# MSYS2/MinGW g++ is the most compatible path with upstream zpaqfranz build instructions.
g++ -O3 -std=c++11 $Upstream -o $Out -pthread -static
Write-Host "built: $Out"
