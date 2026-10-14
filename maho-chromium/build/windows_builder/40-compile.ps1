<#
.SYNOPSIS
  Compile specific ninja targets in out\<OutDir> (used for narrow verification).

.EXAMPLE
  setx MAHO_COMPILE_TARGETS "obj/maho/browser/maho_unified_agent_adapter/maho_unified_agent_adapter.obj"
  run-detached.ps1 -Step 40-compile
  (find object paths with: ninja -C out\Default -t targets all | findstr <file>.obj)
#>
param([string]$Targets = $env:MAHO_COMPILE_TARGETS, [string]$OutDir = 'out\Default', [int]$Jobs = 12)
# Native tools (sccache, gn, ninja, python) write to stderr; check $LASTEXITCODE instead.
$ErrorActionPreference = 'Continue'
if (-not $Targets) { throw 'no targets: pass -Targets or set MAHO_COMPILE_TARGETS' }
$Root = 'C:\src'; $State = Join-Path $Root 'maho-win-builder'
$env:Path = "$Root\depot_tools;$env:Path"; $env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'
Remove-Item env:vs2022_install -EA SilentlyContinue
if (-not $env:vs2026_install) { $env:vs2026_install = [Environment]::GetEnvironmentVariable('vs2026_install', 'User') }
if (-not $env:vs2026_install) { throw 'vs2026_install is not set; run 00-prerequisites.ps1' }
$env:SCCACHE_SERVER_PORT = '4228'
cmd /c "sccache --start-server >nul 2>&1"
$loc = (sccache --show-stats | Select-String 'Cache location').Line
if ($loc -notmatch 's3') { throw "sccache is not on S3: $loc" }
Push-Location (Join-Path $Root 'chromium\src')
try {
  autoninja -C $OutDir -j $Jobs @($Targets -split '\s+' | Where-Object { $_ })
  if ($LASTEXITCODE -ne 0) { throw "autoninja exit $LASTEXITCODE" }
} finally { Pop-Location }
"COMPILE_OK $Targets"
