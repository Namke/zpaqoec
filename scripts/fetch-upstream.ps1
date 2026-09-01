$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path "$PSScriptRoot\..").Path
$Dir = Join-Path $Root 'upstream'
New-Item -ItemType Directory -Force -Path $Dir | Out-Null
$Url = 'https://raw.githubusercontent.com/fcorbelli/zpaqfranz/64.8/zpaqfranz.cpp'
$Out = Join-Path $Dir 'zpaqfranz.cpp'
Invoke-WebRequest -Uri $Url -OutFile $Out
Write-Host "downloaded zpaqfranz 64.8 -> $Out"
