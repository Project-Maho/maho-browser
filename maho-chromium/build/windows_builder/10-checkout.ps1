<#
.SYNOPSIS
  Create (or refresh) the canonical Maho Chromium checkout on the Windows builder.

.DESCRIPTION
  Layout (all under C:\src, outside the user profile and away from OneDrive):
    C:\src\depot_tools                 depot_tools (00-prerequisites.ps1)
    C:\src\maho-browser                git clone of Project-Maho/maho-browser (workspace root)
    C:\src\maho-browser\chromium      NTFS junction -> C:\src\chromium
    C:\src\chromium\src               Chromium checkout pinned to the revision in
                                       maho-chromium/build/CHROMIUM_REVISION
    C:\src\chromium\src\maho          junction -> C:\src\maho-browser\maho-chromium (made by build_maho.py)

  build_maho.py resolves chromium/src relative to the workspace root, so the
  junction keeps the ~100 GB Chromium tree outside the git clone.

.PARAMETER Ref
  maho-browser commit to check out (default: $env:MAHO_REF, else origin/main).
  Prefer MAHO_REF when launching through run-detached.ps1.
#>
param([string]$Ref = $(if ($env:MAHO_REF) { $env:MAHO_REF } else { 'origin/main' }))
# git/gclient write progress to stderr; 'Stop' would turn it into a terminating
# error in PowerShell 5. Every native call below checks $LASTEXITCODE instead.
$ErrorActionPreference = 'Continue'
$Root = 'C:\src'
$State = Join-Path $Root 'maho-win-builder'
New-Item -ItemType Directory -Force -Path $State | Out-Null
function Log($m) { "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m | Tee-Object -FilePath (Join-Path $State 'checkout.log') -Append }
if (-not (Test-Path (Join-Path $State 'prereq.ok'))) { throw 'run 00-prerequisites.ps1 first' }
$env:Path = "$Root\depot_tools;$env:Path"
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'
Remove-Item env:vs2022_install -EA SilentlyContinue
if (-not $env:vs2026_install) { $env:vs2026_install = [Environment]::GetEnvironmentVariable('vs2026_install', 'User') }
if (-not $env:vs2026_install) { throw 'vs2026_install is not set; run 00-prerequisites.ps1' }

$repo = Join-Path $Root 'maho-browser'
if (-not (Test-Path (Join-Path $repo '.git'))) {
  git clone --no-checkout git@github.com:Project-Maho/maho-browser.git $repo
  Log 'maho-browser cloned'
}
# Some tracked paths are not representable on NTFS (e.g. test artifacts under
# maho/crates/maho-ffi/:memory:/). Exclude them so checkout succeeds.
# Git's NTFS path guard rejects such index entries before sparse rules apply,
# so relax it for the index only; the sparse rule keeps them off disk.
git -C $repo config core.protectNTFS false
# Keep LF on disk: overlay patches are produced on macOS/Linux, and Chromium
# tooling does not need CRLF. (Machine-wide autocrlf=true would break git apply.)
git -C $repo config core.autocrlf false
# Build-time credentials (e.g. .secrets/maho/google_desktop_oauth.json for the
# Mail OAuth client baked into maho_mail_ffi) live in .secrets/, which is not in
# the public .gitignore; keep it out of git on the builder.
$exclude = Join-Path $repo '.git\info\exclude'
if (-not (Select-String -Path $exclude -SimpleMatch '.secrets/' -Quiet)) { Add-Content -Path $exclude -Value '.secrets/' }
git -C $repo sparse-checkout set --no-cone '/*' '!/maho/crates/maho-ffi/:memory:/'
git -C $repo fetch --prune origin
# Unpushed local commits can be shipped as a bundle (see docs/development/windows-builder.md).
$bundle = Join-Path $State 'local.bundle'
# Create it with: git bundle create local.bundle origin/main..main main
if (Test-Path $bundle) { git -C $repo fetch $bundle 'refs/heads/main:refs/remotes/local/main'; Log 'fetched local.bundle -> local/main' }
git -C $repo -c advice.detachedHead=false checkout --detach $Ref
if ($LASTEXITCODE -ne 0) { throw "checkout of $Ref failed" }
Log ("maho-browser at " + (git -C $repo rev-parse HEAD))

# Chromium revision = the explicit pin in maho-chromium/build/CHROMIUM_REVISION
# (never inferred from base-patch directory mtimes; several revisions coexist).
$revFile = Join-Path $repo 'maho-chromium\build\CHROMIUM_REVISION'
$rev = (Get-Content -Path $revFile -TotalCount 1).Trim()
if ($env:MAHO_CHROMIUM_REVISION) { $rev = $env:MAHO_CHROMIUM_REVISION }
if ($rev -notmatch '^[0-9a-f]{40}$') { throw "invalid Chromium revision '$rev' (from $revFile)" }
if (-not (Test-Path (Join-Path $repo "maho-chromium\build\chromium_base_patches\$rev") -PathType Container)) {
  throw "no chromium_base_patches directory for Chromium revision $rev"
}
Log "Chromium revision $rev"

$chromium = Join-Path $Root 'chromium'
New-Item -ItemType Directory -Force -Path $chromium | Out-Null
$gclientFile = Join-Path $chromium '.gclient'
if (-not (Test-Path $gclientFile)) {
  Set-Content -Path $gclientFile -Encoding ascii -Value @'
solutions = [
  {
    "name": "src",
    "url": "https://chromium.googlesource.com/chromium/src.git",
    "managed": False,
    "custom_deps": {},
    "custom_vars": {"checkout_pgo_profiles": True},
  },
]
target_os = ["win"]
'@
}
Push-Location $chromium
try {
  if (-not (Test-Path 'src\.git')) {
    git clone --no-checkout --filter=blob:none https://chromium.googlesource.com/chromium/src.git src
  }
  git -C src fetch --depth 1 origin $rev
  git -C src checkout --detach $rev
  # Throttled: googlesource rate-limits parallel fetches (HTTP 429).
  gclient sync --nohooks --no-history --shallow -j 4 --revision "src@$rev"
  if ($LASTEXITCODE -ne 0) { throw "gclient sync exit $LASTEXITCODE" }
  gclient runhooks
  if ($LASTEXITCODE -ne 0) { throw "gclient runhooks exit $LASTEXITCODE" }
} finally { Pop-Location }

# GN refers to the workspace's maho/ as //../../maho (two levels above
# chromium/src, e.g. maho-chromium/browser/updates/BUILD.gn version.txt), so
# C:\src\maho must resolve to the clone's maho/ directory.
$mahoLink = Join-Path $Root 'maho'
if (-not (Test-Path $mahoLink)) { cmd /c mklink /J "$mahoLink" "$repo\maho" | Out-Null; Log 'junction C:\src\maho -> maho-browser\maho' }
$link = Join-Path $repo 'chromium'
if (-not (Test-Path $link)) { cmd /c mklink /J "$link" "$chromium" | Out-Null; Log 'junction maho-browser\chromium -> C:\src\chromium' }
Set-Content -Path (Join-Path $State 'checkout.ok') -Value "$rev $(Get-Date -Format o)"
Log 'CHECKOUT_OK'
