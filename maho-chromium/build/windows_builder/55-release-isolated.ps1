<#
.SYNOPSIS
  Official Windows release build in a dedicated tree that no other job touches.

.DESCRIPTION
  The shared C:\src\maho-browser + C:\src\chromium checkout is also used for
  verifying uncommitted work (step 20 / ad-hoc resync jobs). A release must be
  built only from a committed ref, so this step uses its own workspace:

    C:\rel\maho-browser          clone of MAHO_RELEASE_REF (from local.bundle)
    C:\rel\maho-browser\chromium junction -> C:\rel\chromium
    C:\rel\chromium\src          Chromium at the pinned revision (own gclient sync)

  The checkout is verified clean (no tracked changes) before and after the
  build. Inputs: MAHO_RELEASE_REF (e.g. release/2026.10.14), the bundle at
  C:\src\maho-win-builder\local.bundle, sccache env (step 30), and the
  .secrets files listed in step 50.
#>
$ErrorActionPreference = 'Continue'
$Rel = 'C:\rel'; $State = 'C:\src\maho-win-builder'
$ref = $env:MAHO_RELEASE_REF; if (-not $ref) { $ref = [Environment]::GetEnvironmentVariable('MAHO_RELEASE_REF','User') }
if (-not $ref) { throw 'MAHO_RELEASE_REF is not set' }
$env:Path = "C:\src\depot_tools;$env:Path"; $env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'
if (-not $env:vs2026_install) { $env:vs2026_install = [Environment]::GetEnvironmentVariable('vs2026_install','User') }
$env:CARGO_TARGET_DIR = Join-Path $Rel 'cargo-target'
$env:SCCACHE_SERVER_PORT = '4228'
if (-not $env:MAHO_NINJA_JOBS) { $env:MAHO_NINJA_JOBS = '8' }
function Log($m) { "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m }
New-Item -ItemType Directory -Force -Path $Rel | Out-Null
if ((Get-MpPreference).ExclusionPath -notcontains $Rel) { Add-MpPreference -ExclusionPath $Rel }

$repo = Join-Path $Rel 'maho-browser'
if (-not (Test-Path (Join-Path $repo '.git'))) {
  git clone --no-checkout git@github.com:Project-Maho/maho-browser.git $repo
  git -C $repo config core.protectNTFS false
  git -C $repo config core.autocrlf false
  git -C $repo sparse-checkout set --no-cone '/*' '!/maho/crates/maho-ffi/:memory:/'
  Add-Content (Join-Path $repo '.git\info\exclude') "/chromium","/website","/node_modules","/.secrets",'maho-chromium/third_party/maho/prebuilt/'
}
git -C $repo fetch (Join-Path $State 'local.bundle') "refs/heads/$($ref):refs/remotes/local/$ref" 2>&1 | Write-Output
git -C $repo -c advice.detachedHead=false checkout -f --detach "local/$ref" 2>&1 | Write-Output
if ($LASTEXITCODE -ne 0) { throw "checkout $ref failed" }
# Restore anything a previous run deleted (e.g. through a followed junction).
git -C $repo reset -q --hard
if ((git -C $repo status --porcelain --untracked-files=no | Measure-Object).Count -ne 0) { throw 'release checkout is not clean after reset' }
$head = git -C $repo rev-parse HEAD
Log "release ref $ref = $head version $(Get-Content $repo\maho\version.txt)"

# WebUI toolchain lives inside the release checkout (website\ + node_modules),
# installed from the pinned bun.lock; never linked to the shared tree.
& (Join-Path $PSScriptRoot '15-webui-deps.ps1') -Repo $repo
if ($LASTEXITCODE -ne 0 -and -not $?) { throw 'webui deps failed' }
foreach ($pair in @(@('.secrets','C:\src\maho-browser\.secrets'), @('chromium', (Join-Path $Rel 'chromium')))) {
  $link = Join-Path $repo $pair[0]
  if ($pair[0] -eq 'chromium') { New-Item -ItemType Directory -Force -Path $pair[1] | Out-Null }
  if (-not (Test-Path $link)) { cmd /c mklink /J "$link" "$($pair[1])" | Out-Null }
}
$mahoLink = Join-Path $Rel 'maho'; if (-not (Test-Path $mahoLink)) { cmd /c mklink /J "$mahoLink" "$repo\maho" | Out-Null }

$chromium = Join-Path $Rel 'chromium'
$rev = (Get-ChildItem (Join-Path $repo 'maho-chromium\build\chromium_base_patches') -Directory | Where-Object Name -match '^[0-9a-f]{40}$' | Sort-Object LastWriteTime -Descending | Select-Object -First 1).Name
if (-not (Test-Path (Join-Path $chromium '.gclient'))) {
  Copy-Item 'C:\src\chromium\.gclient' (Join-Path $chromium '.gclient')
}
Push-Location $chromium
try {
  if (-not (Test-Path 'src\.git')) {
    # Reuse objects from the shared checkout instead of re-downloading Chromium.
    git clone --no-checkout --shared C:\src\chromium\src src
  }
  git -C src fetch C:\src\chromium\src $rev 2>&1 | Write-Output
  git -C src -c advice.detachedHead=false checkout -f --detach $rev 2>&1 | Write-Output
  # Junctions into the release checkout must be unlinked (not recursed) before
  # anything in this tree cleans or syncs: gclient -D / git clean follow a
  # junction and delete the release checkout's maho-chromium through it.
  foreach ($j in 'src\maho','src\website') { if (Test-Path $j) { cmd /c rmdir "$j" | Out-Null } }
  git -C src reset -q --hard
  git -C src clean -fdq -e out
  Log "gclient sync -> $rev"
  gclient sync --nohooks --no-history --shallow -j 4 --revision "src@$rev" --force
  if ($LASTEXITCODE -ne 0) { throw "gclient sync exit $LASTEXITCODE" }
  gclient runhooks
  if ($LASTEXITCODE -ne 0) { throw "gclient runhooks exit $LASTEXITCODE" }
} finally { Pop-Location }
$srcWebsite = Join-Path $chromium 'src\website'; if (-not (Test-Path $srcWebsite)) { cmd /c mklink /J "$srcWebsite" "$repo\website" | Out-Null }
if (-not (Test-Path (Join-Path $repo 'maho-chromium\build\scripts\build_maho.py'))) { throw 'release checkout lost maho-chromium' }

cmd /c "sccache --start-server >nul 2>&1"
$loc = (sccache --show-stats | Select-String 'Cache location').Line
if ($loc -notmatch 's3') { throw "sccache is not on S3: $loc" }

Push-Location $repo
try {
  python maho-chromium\build\scripts\build_maho.py --release --out-dir out/Release --skip-codesign --skip-lucide-check --force-args-template
  if ($LASTEXITCODE -ne 0) { throw "build_maho.py --release exit $LASTEXITCODE" }
} finally { Pop-Location }

$after = (git -C $repo status --porcelain --untracked-files=no | Measure-Object).Count
if ((git -C $repo rev-parse HEAD) -ne $head -or $after -ne 0) { throw "release checkout changed during the build (HEAD or $after tracked changes)" }
$out = Join-Path $chromium 'src\out\Release'
foreach ($f in 'chrome.exe','maho_mail_helper.exe','mini_installer.exe','maho.exe') { if (-not (Test-Path (Join-Path $out $f))) { throw "missing release artifact $f" } }
Log "RELEASE_ISOLATED_OK $head"
