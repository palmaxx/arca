# Build the ARCA native tree (core + tools) with MSVC under vcvars64.
#
#   .\build.ps1 [-Preset win-x64-release] [-VsRoot <path>]

param(
    [string]$Preset = 'win-x64-release',
    [string]$VsRoot = 'C:\Program Files\Microsoft Visual Studio\18\Community'
)
$ErrorActionPreference = 'Stop'
$vcvars = Join-Path $VsRoot 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found under $VsRoot" }

cmd /c "`"$vcvars`" >nul && cmake --preset $Preset && cmake --build --preset $Preset"
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
Write-Host "OK -> build\$Preset\bin"
