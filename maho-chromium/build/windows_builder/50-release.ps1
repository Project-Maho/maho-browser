<#
.SYNOPSIS
  Official Windows release build (chrome + maho_mail_helper + mini_installer)
  through the same entry point release pipelines use: build_maho.py --release.

.DESCRIPTION
  Builds into out\Release with maho-chromium/build/config/args_win_release.gn
  (is_official_build=true, non-component). Signing is skipped
  (--skip-codesign): no Windows code-signing certificate is provisioned yet.
  Store (MSIX) packaging is a separate step on top of this output.
  Requires steps 00, 10, 15, 30 (sccache env) and, under
  C:\src\maho-browser\.secrets\maho\, google_desktop_oauth.json (Mail
  OAuth client) and google_chromium_api_key.env (release builds bake the
  Google API key into args.gn).
#>
$ErrorActionPreference = 'Continue'
$Root = 'C:\src'; $State = Join-Path $Root 'maho-win-builder'
$env:Path = "$Root\depot_tools;$env:Path"; $env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'
if (-not $env:vs2026_install) { $env:vs2026_install = [Environment]::GetEnvironmentVariable('vs2026_install', 'User') }
if (-not $env:vs2026_install) { throw 'vs2026_install is not set; run 00-prerequisites.ps1' }
$env:CARGO_TARGET_DIR = Join-Path $Root 'cargo-target'
$env:SCCACHE_SERVER_PORT = '4228'
if (-not $env:MAHO_NINJA_JOBS) { $env:MAHO_NINJA_JOBS = '8' }
cmd /c "sccache --start-server >nul 2>&1"
$loc = (sccache --show-stats | Select-String 'Cache location').Line
if ($loc -notmatch 's3') { throw "sccache is not on S3: $loc" }
Push-Location (Join-Path $Root 'maho-browser')
try {
  python maho-chromium\build\scripts\build_maho.py --release --out-dir out/Release --skip-codesign --skip-lucide-check
  if ($LASTEXITCODE -ne 0) { throw "build_maho.py --release exit $LASTEXITCODE" }
} finally { Pop-Location }
$out = Join-Path $Root 'chromium\src\out\Release'
foreach ($f in 'chrome.exe','maho_mail_helper.exe','mini_installer.exe') {
  if (-not (Test-Path (Join-Path $out $f))) { throw "missing release artifact $f" }
}
Get-ChildItem $out -Filter 'MahoSetup-*.exe' | Format-Table Name,Length,LastWriteTime | Out-String | Write-Output
"RELEASE_OK"
