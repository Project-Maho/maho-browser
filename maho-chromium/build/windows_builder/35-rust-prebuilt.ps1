<#
.SYNOPSIS
  Build the Rust static libraries (maho_ffi.lib, maho_stt_ffi.lib, maho_mail_ffi.lib)
  that Chromium links from maho-chromium/third_party/maho/prebuilt/x64.

.DESCRIPTION
  Wraps maho-chromium/build/scripts/build_maho.py --skip-chromium so the same
  code path as release builds produces the prebuilts. Requires the Rust MSVC
  toolchain (rustup default stable-x86_64-pc-windows-msvc); installs the
  rustup llvm-tools component used for staticlib symbol privatization and
  cbindgen 0.29.2 used to regenerate maho_ffi.h.
#>
$ErrorActionPreference = 'Continue'
$Root = 'C:\src'; $State = Join-Path $Root 'maho-win-builder'
$env:Path = "$Root\depot_tools;$env:Path"; $env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'
if (-not $env:vs2026_install) { $env:vs2026_install = [Environment]::GetEnvironmentVariable('vs2026_install', 'User') }
$env:CARGO_TARGET_DIR = Join-Path $Root 'cargo-target'
# build_maho_core.py privatizes symbols in the staticlibs with llvm-nm/ar/objcopy.
rustup component add llvm-tools 2>&1 | Write-Output
if ($LASTEXITCODE -ne 0) { throw "rustup component add llvm-tools exit $LASTEXITCODE" }
# build_maho_core.py regenerates maho_ffi.h with cbindgen (pinned to the dev-machine version).
$cbindgenVersion = '0.29.2'
$have = (& cbindgen --version 2>$null) -replace '^cbindgen\s+', ''
if ($have -ne $cbindgenVersion) {
  cargo install cbindgen --version $cbindgenVersion --locked 2>&1 | Write-Output
  if ($LASTEXITCODE -ne 0) { throw "cargo install cbindgen exit $LASTEXITCODE" }
}
Push-Location (Join-Path $Root 'maho-browser')
try {
  python maho-chromium\build\scripts\build_maho.py --skip-chromium --skip-lucide-check --skip-sccache-check
  if ($LASTEXITCODE -ne 0) { throw "build_maho.py --skip-chromium exit $LASTEXITCODE" }
} finally { Pop-Location }
$pre = Join-Path $Root 'maho-browser\maho-chromium\third_party\maho\prebuilt\x64'
Get-ChildItem $pre -Filter *.lib | Format-Table Name,Length,LastWriteTime | Out-String | Write-Output
if (-not (Test-Path (Join-Path $pre 'maho_ffi.lib'))) { throw 'maho_ffi.lib was not produced' }
"RUST_PREBUILT_OK"
