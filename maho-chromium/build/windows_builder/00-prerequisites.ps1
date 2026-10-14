<#
.SYNOPSIS
  One-time machine prerequisites for a Maho Chromium build on Windows (admin).

.DESCRIPTION
  Idempotent. Safe to re-run. Installs/enables only what Chromium's Windows
  build needs on top of a stock Windows 11 + Visual Studio 2022 Build Tools:
    * NTFS long paths (Chromium paths exceed MAX_PATH)
    * VS 2026 Build Tools (C++ workload, ATL/MFC, Windows SDK 10.0.28000)
    * Windows SDK debugging tools (required by Chromium's toolchain checks)
    * depot_tools at C:\src\depot_tools
  Writes a marker file C:\src\maho-win-builder\prereq.ok on success.

  See docs/development/windows-builder.md for the full runbook.
#>
$ErrorActionPreference = 'Stop'
$Root = 'C:\src'
$State = Join-Path $Root 'maho-win-builder'
New-Item -ItemType Directory -Force -Path $Root, $State | Out-Null
function Log($m) { "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m | Tee-Object -FilePath (Join-Path $State 'prereq.log') -Append }

# 1. Long paths
$fs = 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem'
if ((Get-ItemProperty $fs).LongPathsEnabled -ne 1) {
  Set-ItemProperty $fs -Name LongPathsEnabled -Value 1 -Type DWord
  Log 'LongPathsEnabled=1 set'
} else { Log 'LongPathsEnabled already 1' }
git config --system core.longpaths true

# 2. Visual Studio 2026 Build Tools (Chromium 154 requires VS >= 18.0 and
#    Windows SDK 10.0.28000; see chromium/src/docs/windows_build_instructions.md).
#    Components: NativeDesktop workload + ATL/MFC + SDK 28000.
$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = if (Test-Path $vswhere) { & $vswhere -products * -version '[18.0,19.0)' -latest -property installationPath } else { $null }
$sdkInclude = 'C:\Program Files (x86)\Windows Kits\10\Include\10.0.28000.0\um\windows.h'
$vsComponents = @(
  'Microsoft.VisualStudio.Workload.VCTools',
  'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
  'Microsoft.VisualStudio.Component.VC.ATL',
  'Microsoft.VisualStudio.Component.VC.ATLMFC',
  'Microsoft.VisualStudio.Component.Windows11SDK.28000'
)
function Test-VsReady($path) {
  if (-not $path) { return $false }
  $msvc = Get-ChildItem (Join-Path $path 'VC\Tools\MSVC') -Directory -EA SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1
  return $msvc -and (Test-Path (Join-Path $msvc.FullName 'atlmfc\include\atlbase.h')) -and (Test-Path $sdkInclude)
}
if (-not (Test-VsReady $vsPath)) {
  $boot = Join-Path $State 'vs_BuildTools.exe'
  # aka.ms/vs/18/stable is the VS 2026 channel (vs/18/release redirects to Bing).
  Invoke-WebRequest -UseBasicParsing -Uri 'https://aka.ms/vs/18/stable/vs_buildtools.exe' -OutFile $boot
  $sig = Get-AuthenticodeSignature $boot
  if ($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'Microsoft Corporation') { throw "VS bootstrapper signature invalid: $($sig.Status)" }
  # Start-Process joins -ArgumentList with spaces; nothing here contains one.
  $vsArgs = @('--quiet', '--wait', '--norestart', '--nocache') + ($vsComponents | ForEach-Object { @('--add', $_) })
  if ($vsPath) { $vsArgs = @('modify', '--installPath', ('"{0}"' -f $vsPath)) + $vsArgs }
  Log ('VS 2026 Build Tools: ' + ($vsArgs -join ' '))
  $p = Start-Process -FilePath $boot -ArgumentList $vsArgs -Wait -PassThru
  Log "VS bootstrapper exit $($p.ExitCode)"
  if ($p.ExitCode -notin 0, 3010) { throw "VS bootstrapper exit $($p.ExitCode) (logs: %TEMP%\dd_*.log)" }
  $vsPath = & $vswhere -products * -version '[18.0,19.0)' -latest -property installationPath
  if (-not (Test-VsReady $vsPath)) { throw "VS 2026 at '$vsPath' is missing ATL/MFC or SDK 10.0.28000" }
} else { Log "VS 2026 ready at $vsPath" }
# Chromium's vs_toolchain.py probes %ProgramFiles%\Microsoft Visual Studio\18\<edition>;
# vs2026_install pins the exact location regardless of where it was installed.
[Environment]::SetEnvironmentVariable('vs2026_install', $vsPath, 'User')
[Environment]::SetEnvironmentVariable('vs2022_install', $null, 'User')
Log "vs2026_install=$vsPath"

# 3. SDK Debugging Tools (cdb/dbghelp) - Chromium's vs_toolchain checks for them
$cdb = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe'
if (-not (Test-Path $cdb)) {
  $sdk = Get-ChildItem 'C:\ProgramData\Package Cache' -Recurse -Filter winsdksetup.exe -EA SilentlyContinue | Sort-Object { $_.VersionInfo.ProductVersion } -Descending | Select-Object -First 1
  if (-not $sdk) { throw 'winsdksetup.exe not found in Package Cache; install Windows SDK 10.0.26100 Debugging Tools manually' }
  Log "Installing Debugging Tools via $($sdk.FullName)"
  $p = Start-Process -FilePath $sdk.FullName -ArgumentList '/features','OptionId.WindowsDesktopDebuggers','/quiet','/norestart' -Wait -PassThru
  if (-not (Test-Path $cdb)) { throw "Debugging Tools install failed (exit $($p.ExitCode))" }
  Log 'Debugging Tools installed'
} else { Log 'Debugging Tools present' }

# 3b. Defender exclusion for the build root. Chromium's Windows build docs
#     recommend it for speed, and it is required: Defender flags the prebuilt
#     CIPD package chromium/third_party/updater/chrome_win_x86 as PUA and
#     quarantines it mid-sync, which makes gclient sync fail.
#     Scope: C:\src only. Remove with: Remove-MpPreference -ExclusionPath C:\src
if ((Get-MpPreference).ExclusionPath -notcontains $Root) {
  Add-MpPreference -ExclusionPath $Root
  Log "Defender exclusion added: $Root"
} else { Log "Defender exclusion present: $Root" }

# 4. depot_tools
$dt = Join-Path $Root 'depot_tools'
if (-not (Test-Path (Join-Path $dt 'gclient.bat'))) {
  git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git $dt
  Log 'depot_tools cloned'
} else { git -C $dt pull --ff-only | Out-Null; Log 'depot_tools updated' }
[Environment]::SetEnvironmentVariable('DEPOT_TOOLS_WIN_TOOLCHAIN', '0', 'User')
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if ($userPath -notlike "*$dt*") { [Environment]::SetEnvironmentVariable('Path', "$dt;$userPath", 'User'); Log 'depot_tools prepended to User PATH' }

Set-Content -Path (Join-Path $State 'prereq.ok') -Value (Get-Date -Format o)
Log 'PREREQ_OK'
