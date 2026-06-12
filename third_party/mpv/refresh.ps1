# Refreshes the vendored libmpv artifacts from the local mpv fork build.
#
#   .\refresh.ps1 [-MpvWorktree <path>] [-VsRoot <path>]
#
# Copies the public headers + libmpv-2.dll from the fork's build worktree and
# generates an MSVC import library (mpv.lib) from the DLL's export table
# (dumpbin -> .def -> lib.exe). Updates PROVENANCE.md with the fork commit.
#
# See DECISIONS.md ADR-002 for why artifacts are vendored rather than built
# in-tree or referenced as a submodule.

param(
    [string]$MpvWorktree = 'C:\DEV\ai-dev\projects\mpv-wt-hdr',
    [string]$VsRoot = 'C:\Program Files\Microsoft Visual Studio\18\Community'
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

$dll = Join-Path $MpvWorktree 'bld\libmpv-2.dll'
$inc = Join-Path $MpvWorktree 'include\mpv'
if (-not (Test-Path $dll)) { throw "libmpv-2.dll not found at $dll (build the fork first: ninja -C bld under MSYS2 UCRT64)" }
if (-not (Test-Path $inc)) { throw "mpv headers not found at $inc" }

# --- copy headers + DLL ------------------------------------------------------
New-Item -ItemType Directory -Force (Join-Path $root 'include\mpv') | Out-Null
New-Item -ItemType Directory -Force (Join-Path $root 'bin') | Out-Null
New-Item -ItemType Directory -Force (Join-Path $root 'lib') | Out-Null
Copy-Item (Join-Path $inc '*.h') (Join-Path $root 'include\mpv\') -Force
Copy-Item $dll (Join-Path $root 'bin\') -Force

# --- vendor the MSYS2 runtime dependency closure ------------------------------
# The fork's libmpv links FFmpeg/libplacebo/etc. dynamically from the MSYS2
# UCRT64 runtime; enumerate the load-time closure with ldd and vendor it so
# the player is self-contained (incl. libdovi - Dolby Vision RPU support).
# ldd must run against the SOURCE DLL in the fork worktree: against the
# vendored copy, Windows resolves every dependency from the already-vendored
# closure sitting next to it (module dir precedes PATH), no path matches
# /ucrt64/bin/, and the sanity check below trips on every re-refresh.
$env:MSYSTEM = 'UCRT64'
$dllMsys = '/' + ($dll -replace ':', '' -replace '\\', '/')
$lddOut = C:\msys64\usr\bin\bash.exe -lc "ldd '$dllMsys'"
$deps = $lddOut | ForEach-Object {
    if ($_ -match '=>\s+(/ucrt64/bin/\S+\.dll)') { $Matches[1] }
} | Sort-Object -Unique
if ($deps.Count -lt 20) { throw "ldd closure suspiciously small ($($deps.Count)) - MSYS2 UCRT64 runtime missing?" }
foreach ($d in $deps) {
    $win = $d -replace '^/ucrt64/bin/', 'C:\msys64\ucrt64\bin\'
    Copy-Item $win (Join-Path $root 'bin\') -Force
}
Write-Host "vendored $($deps.Count) MSYS2 runtime DLLs"

# --- generate MSVC import library from the DLL export table ------------------
$vcvars = Join-Path $VsRoot 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found under $VsRoot" }

$defPath = Join-Path $root 'lib\libmpv-2.def'
$libPath = Join-Path $root 'lib\mpv.lib'

$exports = cmd /c "`"$vcvars`" >nul 2>&1 && dumpbin /exports `"$dll`""
if ($LASTEXITCODE -ne 0) { throw 'dumpbin failed' }

# dumpbin export rows: "  ordinal  hint  RVA  name". Keep the name column.
$names = $exports | ForEach-Object {
    if ($_ -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]{8}\s+(\S+)') { $Matches[1] }
} | Where-Object { $_ }
if ($names.Count -lt 10) { throw "export parse produced only $($names.Count) symbols - dumpbin format change?" }

@('LIBRARY libmpv-2.dll', 'EXPORTS') + $names | Set-Content -Encoding ascii $defPath
cmd /c "`"$vcvars`" >nul 2>&1 && lib /nologo /def:`"$defPath`" /machine:x64 /out:`"$libPath`"" | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'lib.exe failed' }

# --- provenance ---------------------------------------------------------------
$commit = git -C $MpvWorktree rev-parse HEAD
$branch = git -C $MpvWorktree rev-parse --abbrev-ref HEAD
$dllInfo = Get-Item (Join-Path $root 'bin\libmpv-2.dll')
@"
# Vendored libmpv provenance

- Source fork worktree: ``$MpvWorktree``
- Branch: ``$branch``
- Commit: ``$commit``
- Built: MSYS2 UCRT64 (``ninja -C bld``); DLL timestamp $($dllInfo.LastWriteTime.ToString('yyyy-MM-dd HH:mm'))
- Vendored: $(Get-Date -Format 'yyyy-MM-dd HH:mm') by refresh.ps1
- Exports in mpv.lib: $($names.Count)
- MSYS2 UCRT64 runtime closure: $($deps.Count) DLLs (load-time deps via ldd;
  includes libdovi = Dolby Vision RPU support in libplacebo)

The fork adds the gpu-next libmpv render backend (``pl-d3d11`` / ``pl-vulkan`` /
``pl-opengl``) and ``MPV_RENDER_PARAM_TARGET_COLORSPACE`` (client API 2.7).
Canonical history: WSL ``~/mpv-fork``; audit map:
``C:\DEV\ai-dev\projects\mpv-src\_refactor\MERGE_AUDIT_HANDOFF.md``.
"@ | Set-Content -Encoding utf8 (Join-Path $root 'PROVENANCE.md')

Write-Host "OK: vendored libmpv-2.dll ($([math]::Round($dllInfo.Length/1MB,1)) MB), $($names.Count) exports -> mpv.lib, commit $($commit.Substring(0,9))"
