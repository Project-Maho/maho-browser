<#
.SYNOPSIS
  Overlay an uncommitted working-tree diff from a dev machine onto C:\src\maho-browser.

.DESCRIPTION
  Used to verify work that is not committed yet. Inputs, both produced on the
  dev machine and copied into C:\src\maho-win-builder:
    overlay.patch    git diff --binary HEAD   (HEAD must equal MAHO_REF checked out by 10-checkout)
    untracked.tgz    tar of new untracked files (optional)
  The checkout is first reset to its pinned commit so the overlay is reproducible.
#>
# git writes progress to stderr; under 'Stop' PowerShell 5 turns that into a
# terminating NativeCommandError, so rely on $LASTEXITCODE instead.
$ErrorActionPreference = 'Continue'
$Root = 'C:\src'; $State = Join-Path $Root 'maho-win-builder'; $repo = Join-Path $Root 'maho-browser'
function Log($m) { "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m | Tee-Object -FilePath (Join-Path $State 'overlay.log') -Append }
$patch = Join-Path $State 'overlay.patch'
if (-not (Test-Path $patch)) { throw "missing $patch" }
git -C $repo reset --hard -q HEAD
# Keep build products and local-only inputs that are not tracked: Rust
# prebuilts (step 35), the WebUI toolchain (step 15) and build credentials.
git -C $repo clean -fdq -e chromium -e 'maho-chromium/third_party/maho/prebuilt/' -e website -e node_modules -e .secrets
git -C $repo apply --whitespace=nowarn --exclude='maho/crates/maho-ffi/:memory:/*' $patch
if ($LASTEXITCODE -ne 0) { throw "git apply failed ($LASTEXITCODE)" }
$untracked = Join-Path $State 'untracked.tgz'
if (Test-Path $untracked) { tar -xzf $untracked -C $repo; if ($LASTEXITCODE -ne 0) { throw 'untracked extract failed' } }
Log ("overlay applied on " + (git -C $repo rev-parse HEAD) + "; changed files: " + ((git -C $repo status --porcelain | Measure-Object).Count))
Set-Content -Path (Join-Path $State 'overlay.ok') -Value (Get-Date -Format o)
Log 'OVERLAY_OK'
