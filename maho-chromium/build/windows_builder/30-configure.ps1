<#
.SYNOPSIS
  Configure sccache (S3/MinIO) and generate out\<OutDir> for a Maho Windows build.

.DESCRIPTION
  * sccache credentials come from C:\src\maho-win-builder\sccache.env
    (KEY=VALUE lines, never committed; copy it from the owner's secrets vault).
    They are persisted at User scope so a later SSH/schtasks session keeps the
    S3 backend instead of silently falling back to local disk.
  * Applies the tracked Chromium overrides and writes args.gn from
    maho-chromium/build/config/args_win[_debug|_release].gn plus
    cc_wrapper="sccache" and a component build for fast incremental compiles.
#>
param([string]$OutDir = 'out\Default', [ValidateSet('debug','release')][string]$Config = 'debug')
# Native tools (sccache, gn, ninja, python) write to stderr; check $LASTEXITCODE instead.
$ErrorActionPreference = 'Continue'
$Root = 'C:\src'; $State = Join-Path $Root 'maho-win-builder'
function Log($m) { "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m | Tee-Object -FilePath (Join-Path $State 'configure.log') -Append }
$env:Path = "$Root\depot_tools;$env:Path"; $env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'
Remove-Item env:vs2022_install -EA SilentlyContinue
if (-not $env:vs2026_install) { $env:vs2026_install = [Environment]::GetEnvironmentVariable('vs2026_install', 'User') }
if (-not $env:vs2026_install) { throw 'vs2026_install is not set; run 00-prerequisites.ps1' }
$repo = Join-Path $Root 'maho-browser'; $src = Join-Path $Root 'chromium\src'

$envFile = Join-Path $State 'sccache.env'
if (-not (Test-Path $envFile)) { throw "missing $envFile (SCCACHE_BUCKET/ENDPOINT/REGION/S3_KEY_PREFIX/S3_USE_SSL/AWS_ACCESS_KEY_ID/AWS_SECRET_ACCESS_KEY)" }
$env:SCCACHE_SERVER_PORT = '4228'
cmd /c "sccache --stop-server >nul 2>&1"
foreach ($line in Get-Content $envFile) {
  if ($line -match '^\s*([A-Z_]+)=(.*)$') {
    [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'User')
    Set-Item -Path "env:$($Matches[1])" -Value $Matches[2]
  }
}
[Environment]::SetEnvironmentVariable('SCCACHE_DIR', $null, 'User'); Remove-Item env:SCCACHE_DIR -EA SilentlyContinue
$env:SCCACHE_SERVER_PORT = '4228'; [Environment]::SetEnvironmentVariable('SCCACHE_SERVER_PORT', '4228', 'User')
cmd /c "sccache --start-server >nul 2>&1"
$loc = (sccache --show-stats | Select-String 'Cache location').Line
Log "sccache: $loc"
if ($loc -notmatch 's3') { throw "sccache is not on S3: $loc" }

python (Join-Path $repo 'maho-chromium\build\scripts\apply_chromium_src_overrides.py') --chromium-src $src --platform win
if ($LASTEXITCODE -ne 0) { throw "apply_chromium_src_overrides exit $LASTEXITCODE" }
if (-not (Test-Path (Join-Path $src 'maho'))) { cmd /c mklink /J "$src\maho" "$repo\maho-chromium" | Out-Null }

$template = Join-Path $repo ("maho-chromium\build\config\args_win_{0}.gn" -f $Config)
$out = Join-Path $src $OutDir
New-Item -ItemType Directory -Force -Path $out | Out-Null
$component = if ($Config -eq 'debug') { 'true' } else { 'false' }
# Drop template keys this script owns, then append them once (GN rejects duplicates).
$lines = Get-Content $template | Where-Object { $_ -notmatch '^\s*(is_component_build|symbol_level|use_siso|cc_wrapper)\s*=' }
$lines += ''
$lines += '# --- maho windows_builder/30-configure.ps1 ---'
$lines += 'cc_wrapper = "sccache"'
$lines += 'use_siso = false'
$lines += "is_component_build = $component"
$lines += 'symbol_level = 1'
Set-Content -Path (Join-Path $out 'args.gn') -Value $lines -Encoding ascii
Push-Location $src; try { gn gen $OutDir; if ($LASTEXITCODE -ne 0) { throw "gn gen exit $LASTEXITCODE" } } finally { Pop-Location }
Set-Content -Path (Join-Path $State 'configure.ok') -Value "$OutDir $Config $(Get-Date -Format o)"
Log 'CONFIGURE_OK'
