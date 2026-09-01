param(
  [string]$Upstream = "$PSScriptRoot\..\upstream\zpaqfranz.cpp",
  [string]$Out = "$PSScriptRoot\..\build\zpaqoec.exe",
  [string]$Compiler = "",
  [string]$InstallTo = ""
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

function Probe-MinGWCompiler([string]$Candidate, [bool]$Explicit = $false) {
  if ([string]::IsNullOrWhiteSpace($Candidate)) { return $null }

  $resolved = $null
  try {
    # A user supplied full/relative path is a literal filesystem path first.
    # This avoids PATH/Get-Command alias resolution from substituting Cygwin g++.
    if (Test-Path -LiteralPath $Candidate -PathType Leaf) {
      $resolved = (Resolve-Path -LiteralPath $Candidate).Path
    } else {
      $cmd = Get-Command $Candidate -CommandType Application -ErrorAction Stop
      $resolved = $cmd.Source
      if ([string]::IsNullOrWhiteSpace($resolved)) { $resolved = $cmd.Path }
    }
  } catch {
    if ($Explicit) {
      throw "Compiler passed with -Compiler was not found: $Candidate"
    }
    return $null
  }

  $targetLines = @()
  $exit = $null
  try {
    $targetLines = @(& $resolved -dumpmachine 2>&1)
    $exit = $LASTEXITCODE
  } catch {
    if ($Explicit) {
      throw "Failed to execute compiler '$resolved -dumpmachine': $($_.Exception.Message)"
    }
    return $null
  }

  $target = (($targetLines | Select-Object -First 1) -as [string])
  if ($null -ne $target) { $target = $target.Trim() }

  if ($exit -ne 0 -or [string]::IsNullOrWhiteSpace($target)) {
    if ($Explicit) {
      $joined = ($targetLines -join [Environment]::NewLine)
      throw @"
Compiler passed with -Compiler could not be probed:
  compiler: $resolved
  command:  -dumpmachine
  exit:     $exit
  output:   $joined
"@
    }
    return $null
  }

  if ($target -notmatch '(?i)(mingw|w64)') {
    if ($Explicit) {
      throw @"
Compiler passed with -Compiler is not a native Windows MinGW-w64 compiler:
  compiler: $resolved
  target:   $target
Expected -dumpmachine to contain 'mingw' or 'w64' (for example x86_64-w64-mingw32).
"@
    }
    return $null
  }

  return [pscustomobject]@{ Path = $resolved; Target = $target }
}

# If -Compiler was supplied, it is authoritative. Never silently fall back to PATH.
$Selected = $null
if (-not [string]::IsNullOrWhiteSpace($Compiler)) {
  $Selected = Probe-MinGWCompiler $Compiler $true
} else {
  $candidates = @()
  if (-not [string]::IsNullOrWhiteSpace($env:ZPAQFRANZ_GXX)) {
    $candidates += $env:ZPAQFRANZ_GXX
  }
  $candidates += @(
    'C:\msys64\ucrt64\bin\g++.exe',
    'C:\Programs\msys64\ucrt64\bin\g++.exe',
    'C:\msys64\mingw64\bin\g++.exe',
    'C:\Programs\msys64\mingw64\bin\g++.exe',
    'C:\mingw64\bin\g++.exe',
    'C:\mingw\bin\g++.exe',
    'x86_64-w64-mingw32-g++.exe',
    'g++.exe'
  )

  foreach ($candidate in ($candidates | Select-Object -Unique)) {
    $probe = Probe-MinGWCompiler $candidate $false
    if ($null -ne $probe) { $Selected = $probe; break }
  }
}

if ($null -eq $Selected) {
  $pathGxx = $null
  $pathTarget = $null
  try {
    $pathCmd = Get-Command g++.exe -CommandType Application -ErrorAction Stop
    $pathGxx = $pathCmd.Source
    if ([string]::IsNullOrWhiteSpace($pathGxx)) { $pathGxx = $pathCmd.Path }
    $pathTarget = (& $pathGxx -dumpmachine 2>$null | Select-Object -First 1)
  } catch {}
  $detail = if ($pathGxx) { " PATH g++=$pathGxx target=$pathTarget" } else { "" }
  throw @"
No Windows-targeting MinGW g++ was found.$detail
Install/use MSYS2 UCRT64 (recommended by upstream) or pass it explicitly:
  .\scripts\build-windows.ps1 -Compiler C:\Programs\msys64\ucrt64\bin\g++.exe
Or set:
  `$env:ZPAQFRANZ_GXX='C:\Programs\msys64\ucrt64\bin\g++.exe'
The compiler target reported by `-dumpmachine` must contain 'mingw' or 'w64'.
"@
}

$Cxx = $Selected.Path
Write-Host "compiler: $Cxx"
Write-Host "target:   $($Selected.Target)"

$OutDir = Split-Path -Parent $Out
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# Native MSYS2/MinGW executables spawn cc1plus/as/ld and load runtime DLLs
# from the toolchain bin directory. Upstream's own Windows batch prepends this
# directory to PATH before compiling. Do the same here so invoking g++.exe
# directly from PowerShell is reliable even if PATH currently prefers Cygwin.
$CompilerDir = Split-Path -Parent $Cxx
$OldPath = $env:PATH
if (($env:PATH -split ';') -notcontains $CompilerDir) {
  $env:PATH = "$CompilerDir;$env:PATH"
}
Write-Host "toolchain PATH+: $CompilerDir"

# Explicit include root is needed for:
#   #include "extensions/zpaqfranz_ext.hpp"
$CompileArgs = @(
  '-O3',
  '-std=c++11',
  "-I$UpstreamDir",
  $UpstreamPath,
  '-o', $Out,
  '-pthread',
  '-static',
  '-lurlmon'
)

$CompileLog = "$Out.compile.log"
try {
  # Capture both stdout and stderr, then replay it. This avoids PowerShell's
  # native-command stderr handling hiding the real compiler/linker diagnostic.
  $OldEap = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try {
    $CompilerOutput = @(& $Cxx @CompileArgs 2>&1)
    $CompileExit = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $OldEap
  }

  $CompilerText = (($CompilerOutput | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine)
  Set-Content -LiteralPath $CompileLog -Value $CompilerText -Encoding UTF8
  if (-not [string]::IsNullOrWhiteSpace($CompilerText)) {
    Write-Host $CompilerText
  }

  if ($CompileExit -ne 0) {
    throw @"
MinGW g++ failed with exit code $CompileExit
compiler: $Cxx
target:   $($Selected.Target)
log:      $CompileLog
"@
  }
} finally {
  $env:PATH = $OldPath
}

if (-not (Test-Path -LiteralPath $Out)) { throw "compiler returned success but output is missing: $Out" }
$BuiltExe = (Resolve-Path -LiteralPath $Out).Path
Write-Host "built: $BuiltExe"
Write-Host "compile log: $CompileLog"

# Mandatory runtime smoke gate. A successful compile is not enough: the final
# Windows executable must actually intercept OEC commands before upstream's
# unknown-command/help parser. This catches wrong entry-point injection.
function Assert-OecOutput([string]$Label, [string[]]$CommandArgs, [string]$Needle, [int]$ExpectedExit = 0) {
  $OldEap2 = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try {
    $lines = @(& $BuiltExe @CommandArgs 2>&1)
    $rc = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $OldEap2
  }
  $text = (($lines | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine)
  if ($rc -ne $ExpectedExit -or $text -notlike "*$Needle*") {
    throw @"
OEC runtime smoke test FAILED: $Label
  exe:      $BuiltExe
  exit:     $rc (expected $ExpectedExit)
  expected: $Needle
  output:
$text
The binary compiled, but the OEC dispatcher is not on the executable command path.
"@
  }
  Write-Host "smoke PASS: $Label"
}

Assert-OecOutput -Label 'no-arg OEC help' -CommandArgs @() -Needle 'OEC (Optimize + Error Correction)'
Assert-OecOutput -Label 'oec_h dispatcher' -CommandArgs @('oec_h') -Needle 'OEC (Optimize + Error Correction)'
Assert-OecOutput -Label 'oec_version dispatcher' -CommandArgs @('oec_version') -Needle 'zpaqoec OEC overlay 0.3.4'
# Argument-sensitive smoke: this must reach the oecinit parser (not no-arg help).
# Missing ARCHIVE is expected to return 2 and print oecinit usage.
Assert-OecOutput -Label 'oecinit argv dispatch' -CommandArgs @('oecinit') -Needle 'Initialize/retrofit OEC archive' -ExpectedExit 2

# Make stale/shadowed copies obvious. The freshly built executable is normally
# under repo\\build; invoking bare `zpaqoec.exe` elsewhere may run an older copy.
try {
  $Cmd = Get-Command zpaqoec.exe -CommandType Application -ErrorAction Stop
  $ResolvedCommand = $Cmd.Source
  if ([string]::IsNullOrWhiteSpace($ResolvedCommand)) { $ResolvedCommand = $Cmd.Path }
  if (-not [string]::IsNullOrWhiteSpace($ResolvedCommand)) {
    $ResolvedCommand = (Resolve-Path -LiteralPath $ResolvedCommand).Path
    if ($ResolvedCommand -ne $BuiltExe) {
      Write-Warning "Bare 'zpaqoec.exe' currently resolves to a DIFFERENT file: $ResolvedCommand"
      Write-Warning "Fresh build is: $BuiltExe"
      Write-Warning "Run the fresh build by full path, copy/install it, or use -InstallTo."
    }
  }
} catch {}

if (-not [string]::IsNullOrWhiteSpace($InstallTo)) {
  $InstallParent = Split-Path -Parent $InstallTo
  if (-not [string]::IsNullOrWhiteSpace($InstallParent)) {
    New-Item -ItemType Directory -Force -Path $InstallParent | Out-Null
  }
  Copy-Item -LiteralPath $BuiltExe -Destination $InstallTo -Force
  $Installed = (Resolve-Path -LiteralPath $InstallTo).Path
  Write-Host "installed: $Installed"
  # Validate the copied binary too.
  $SavedBuilt = $BuiltExe
  $BuiltExe = $Installed
  try {
    Assert-OecOutput -Label 'installed oec_version dispatcher' -CommandArgs @('oec_version') -Needle 'zpaqoec OEC overlay 0.3.4'
  } finally {
    $BuiltExe = $SavedBuilt
  }
}

Write-Host "OEC Windows build verified. Test with:"
Write-Host "  & '$BuiltExe' oec_version"
Write-Host "  & '$BuiltExe' oec_h"
