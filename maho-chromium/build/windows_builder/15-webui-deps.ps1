<#
.SYNOPSIS
  Provide the WebUI toolchain (esbuild, tailwindcss, react, lucide-react) that
  the Maho WebUI GN actions resolve from <workspace>\website\node_modules.

.DESCRIPTION
  maho-chromium/build/scripts/run_tailwind.py and the bundle_react.mjs scripts
  import from ../../../../website/node_modules. The website package lives in
  the private maho-cloud repo, so the builder only needs its package.json +
  bun.lock (copied into C:\src\maho-win-builder\website\) to install the
  exact locked dependency set. Installs into C:\src\maho-browser\website
  (kept out of git via .git/info/exclude).
#>
$ErrorActionPreference = 'Continue'
$Root = 'C:\src'; $State = Join-Path $Root 'maho-win-builder'
$repo = Join-Path $Root 'maho-browser'
$srcDir = Join-Path $State 'website'
$dst = Join-Path $repo 'website'
foreach ($f in 'package.json','bun.lock','bunfig.toml') { if (-not (Test-Path (Join-Path $srcDir $f)) -and $f -ne 'bunfig.toml') { throw "missing $srcDir\$f" } }
New-Item -ItemType Directory -Force -Path $dst | Out-Null
Copy-Item (Join-Path $srcDir '*') $dst -Force
$exclude = Join-Path $repo '.git\info\exclude'
if (-not (Select-String -Path $exclude -SimpleMatch 'website/' -Quiet)) { Add-Content -Path $exclude -Value 'website/' }
Push-Location $dst
try {
  # Not --frozen-lockfile: the lock is written on macOS and bun re-resolves
  # Windows-only optional packages, which it reports as "lockfile had changes".
  # Versions still come from the copied bun.lock; the rewrite stays on the builder.
  bun install --ignore-scripts 2>&1 | Write-Output
  if ($LASTEXITCODE -ne 0) { throw "bun install exit $LASTEXITCODE" }
} finally { Pop-Location }
# These are imported by the Maho WebUI sources but NOT declared in
# website/package.json; dev machines only have them because they were installed
# ad hoc. Pin the exact versions present on the reference Mac (2026-10-13).
# Follow-up: declare them in the website package (or a dedicated WebUI package).
$undeclared = @(
  '@codemirror/lang-css@6.3.1', '@codemirror/state@6.6.0', '@codemirror/theme-one-dark@6.1.3', '@codemirror/view@6.43.0',
  '@codemirror/commands@6.10.3', '@codemirror/language@6.12.3', '@codemirror/autocomplete@6.20.2', '@codemirror/lint@6.9.6',
  '@codemirror/search@6.7.0', '@radix-ui/react-context-menu@2.3.1', '@radix-ui/react-dropdown-menu@2.1.18', '@radix-ui/react-popover@1.1.17',
  '@radix-ui/react-separator@1.1.10', '@radix-ui/react-tabs@1.1.15', '@radix-ui/react-toggle-group@1.1.13', 'codemirror@6.0.2',
  'marked@17.0.6', 'sonner@1.7.4', 'tw-animate-css@1.4.0', '@tailwindcss/cli@4.3.1',
  'esbuild@0.28.1'
)
Push-Location $dst
try {
  bun add --ignore-scripts --exact @undeclared 2>&1 | Write-Output
  if ($LASTEXITCODE -ne 0) { throw "bun add (undeclared WebUI deps) exit $LASTEXITCODE" }
} finally { Pop-Location }
# Tailwind and esbuild resolve bare imports by walking up from the source file
# (maho-chromium\browser\resources\...), so the workspace root needs a
# node_modules, exactly like the dev Mac's maho-browser/node_modules symlink.
$rootModules = Join-Path $repo 'node_modules'
if (-not (Test-Path $rootModules)) { cmd /c mklink /J "$rootModules" "$dst\node_modules" | Out-Null }
if (-not (Select-String -Path $exclude -SimpleMatch '/node_modules' -Quiet)) { Add-Content -Path $exclude -Value '/node_modules' }
foreach ($m in 'esbuild\lib\main.js','tailwindcss\package.json','sonner\package.json','codemirror\package.json','tw-animate-css\package.json') { if (-not (Test-Path (Join-Path $dst "node_modules\$m"))) { throw "missing node_modules\$m" } }
"WEBUI_DEPS_OK"
