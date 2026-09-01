param(
  [string]$Upstream = "$PSScriptRoot\..\upstream\zpaqfranz.cpp",
  [string]$Out = "$PSScriptRoot\..\build\zpaqfranz-trunkec.exe",
  [string]$Compiler = ""
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

function Test-MinGWCompiler([string]$Path) {
  if ([string]::IsNullOrWhiteSpace($Path)) { return $null }
  try {
    $resolved = (Get-Command $Path -ErrorAction Stop).Source
  } catch {
    if (Test-Path -LiteralPath $Path) { $resolved = (Resolve-Path -LiteralPath $Path).Path }
    else { return $null }
  }
  $target = (& $resolved -dumpmachine 2>$null | Select-Object -First 1)
  if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($target)) { return $null }
  $target = $target.Trim()
  if ($target -notmatch '(?i)(mingw|w64)') { return $null }
  return [pscustomobject]@{ Path = $resolved; Target = $target }
}

# zpaqfranz's Windows source must be compiled with a Windows-targeting MinGW toolchain.
# Do NOT silently accept a Unix/Cygwin g++ found first in PATH: that selects the POSIX
# code paths (fseeko/ftello/fileno/ftruncate/usleep/realpath/select...) and produces a
# wall of misleading missing-symbol errors.
$candidates = @()
if (-not [string]::IsNullOrWhiteSpace($Compiler)) { $candidates += $Compiler }
if (-not [string]::IsNullOrWhiteSpace($env:ZPAQFRANZ_GXX)) { $candidates += $env:ZPAQFRANZ_GXX }
$candidates += @(
  'C:\msys64\ucrt64\bin\g++.exe',
  'C:\msys64\mingw64\bin\g++.exe',
  'C:\mingw64\bin\g++.exe',
  'C:\mingw\bin\g++.exe',
  'x86_64-w64-mingw32-g++.exe',
  'g++.exe'
)

$Selected = $null
foreach ($candidate in ($candidates | Select-Object -Unique)) {
  $probe = Test-MinGWCompiler $candidate
  if ($null -ne $probe) { $Selected = $probe; break }
}

if ($null -eq $Selected) {
  $pathGxx = $null
  $pathTarget = $null
  try {
    $pathGxx = (Get-Command g++.exe -ErrorAction Stop).Source
    $pathTarget = (& $pathGxx -dumpmachine 2>$null | Select-Object -First 1)
  } catch {}
  $detail = if ($pathGxx) { " PATH g++=$pathGxx target=$pathTarget" } else { "" }
  throw @"
No Windows-targeting MinGW g++ was found.$detail
Install/use MSYS2 UCRT64 (recommended by upstream) or pass it explicitly:
  .\scripts\build-windows.ps1 -Compiler C:\msys64\ucrt64\bin\g++.exe
Or set:
  `$env:ZPAQFRANZ_GXX='C:\msys64\ucrt64\bin\g++.exe'
The compiler target reported by `-dumpmachine` must contain 'mingw' or 'w64'.
"@
}

$Cxx = $Selected.Path
Write-Host "compiler: $Cxx"
Write-Host "target:   $($Selected.Target)"

$OutDir = Split-Path -Parent $Out
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# Minimal upstream-compatible Windows build. Explicit include root is needed for:
#   #include "extensions/zpaqfranz_ext.hpp"
& $Cxx -O3 -std=c++11 "-I$UpstreamDir" "$UpstreamPath" -o "$Out" -pthread -static
if ($LASTEXITCODE -ne 0) { throw "MinGW g++ failed with exit code $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $Out)) { throw "compiler returned success but output is missing: $Out" }
Write-Host "built: $Out"
