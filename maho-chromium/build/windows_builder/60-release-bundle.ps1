<#
.SYNOPSIS
  Bundle a finished out\Release build into release artifacts without publishing:
  MahoSetup-<v>.exe, update-win.json (signed envelope), the Store MSIX, and
  release-notes.html, via release_local.py --skip-build --no-publish.

.DESCRIPTION
  Runs from the step-55 release tree (C:\rel\maho-browser), never from the
  shared verification checkout. Requires step 55 to have produced out\Release, plus the Sparkle Ed25519 seed
  for the update envelope at %USERPROFILE%\.secrets\signing\sparkle_ed_private.pem
  (or SPARKLE_ED_PRIVATE_KEY already set). Publishing (GitHub upload, relay
  registration, finalize) is a separate, explicit step.
#>
$ErrorActionPreference = 'Continue'
$env:Path = "C:\src\depot_tools;$env:Path"
$repo = 'C:\rel\maho-browser'
if ((git -C $repo status --porcelain --untracked-files=no | Measure-Object).Count -ne 0) { throw 'release tree has tracked changes' }
if (-not $env:SPARKLE_ED_PRIVATE_KEY) {
  $pem = Join-Path $env:USERPROFILE '.secrets\signing\sparkle_ed_private.pem'
  if (-not (Test-Path $pem)) { throw "missing Sparkle seed: $pem" }
  $env:SPARKLE_ED_PRIVATE_KEY = (Get-Content $pem -Raw).Trim()
}
# generate_update_manifests.py signs update-win.json with Ed25519 via the
# 'cryptography' package; package_msix.py needs no extra modules.
python -c "import cryptography" 2>$null
if ($LASTEXITCODE -ne 0) {
  python -m pip install --user --disable-pip-version-check cryptography 2>&1 | Write-Output
  python -c "import cryptography"; if ($LASTEXITCODE -ne 0) { throw 'cryptography is not importable' }
}
Push-Location $repo
try {
  python maho-chromium\build\scripts\release_local.py --out-dir out/Release --skip-build --no-publish
  if ($LASTEXITCODE -ne 0) { throw "release_local.py --skip-build exit $LASTEXITCODE" }
} finally { Pop-Location }
"RELEASE_BUNDLE_OK"
