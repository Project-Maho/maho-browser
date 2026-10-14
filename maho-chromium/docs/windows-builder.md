# Windows builder runbook

How to stand up and use a Windows machine that builds and verifies the Maho
Chromium overlay. Every step is a script in
[`maho-chromium/build/windows_builder/`](../build/windows_builder/), so a
rebuilt or reinstalled machine reaches the same state by running them in order.

## Layout

Everything lives under `C:\src`: a short path (Chromium paths are long), outside
the user profile and away from OneDrive sync.

| Path | What |
|------|------|
| `C:\src\depot_tools` | depot_tools |
| `C:\src\maho-browser` | git clone of `Project-Maho/maho-browser` (the workspace root) |
| `C:\src\maho-browser\chromium` | NTFS junction to `C:\src\chromium` |
| `C:\src\chromium\src` | Chromium, pinned to the revision named by `maho-chromium/build/chromium_base_patches/<rev>` |
| `C:\src\chromium\src\maho` | junction to `C:\src\maho-browser\maho-chromium` |
| `C:\src\maho-browser\node_modules` | junction to `website\node_modules` (Tailwind resolves `@import "tailwindcss"` upward from the overlay sources) |
| `C:\src\maho` | junction to `C:\src\maho-browser\maho`; GN reaches it as `//../../maho` (e.g. `version.txt`) |
| `C:\src\chromium\src\out\Default` | debug component build (fast incremental compiles) |
| `C:\src\cargo-target` | Cargo target dir for the Rust prebuilts |
| `C:\src\maho-win-builder` | step logs, `.done`/`.failed` sentinels, `sccache.env`, `local.bundle`, `overlay.patch` |

`build_maho.py` resolves `chromium/src` relative to the workspace root, so the
junction keeps the ~100 GB Chromium tree outside the git clone.

## Steps

Copy the scripts to `C:\src\maho-win-builder\scripts\` and run each step
detached. SSH drops kill child processes, so every long step goes through
`run-detached.ps1`, which registers a demand-only scheduled task (no triggers,
Normal priority 4 — the default priority 7 trips Chromium's
`CheckProcessPriority`) and writes `<step>.out` plus a `<step>.done` or
`<step>.failed` sentinel:

```powershell
powershell -File C:\src\maho-win-builder\scripts\run-detached.ps1 -Step 00-prerequisites
```

Steps take their parameters from environment variables (set with `setx`)
rather than `-StepArgs`, because the scheduled-task command line does not
preserve nested quoting.

| Step | Does | Inputs |
|------|------|--------|
| `00-prerequisites` | NTFS long paths, VS 2026 Build Tools (C++ workload, ATL/MFC, SDK 10.0.28000), SDK Debugging Tools, Defender exclusion for `C:\src`, depot_tools, `vs2026_install` | admin |
| `10-checkout` | clones `maho-browser`, checks out `MAHO_REF`, writes `.gclient`, `gclient sync` at the pinned revision, creates the junctions | `MAHO_REF` (default `origin/main`), optional `local.bundle` |
| `20-overlay-worktree` | resets the clone and applies uncommitted work from a dev machine | `overlay.patch`, optional `untracked.tgz` |
| `15-webui-deps` | installs the WebUI toolchain (esbuild, tailwindcss, react) into `C:\src\maho-browser\website\node_modules` from the private `website` package's `package.json` + `bun.lock` | `C:\src\maho-win-builder\website\{package.json,bun.lock}` |
| `30-configure` | puts sccache on the S3/MinIO backend, applies Chromium overrides, writes `args.gn`, `gn gen` | `sccache.env` |
| `35-rust-prebuilt` | adds rustup `llvm-tools` and cbindgen 0.29.2, builds `maho_ffi.lib` and the other Rust prebuilts via `build_maho.py --skip-chromium` | Rust MSVC toolchain |
| `40-compile` | `autoninja` on specific targets | `MAHO_COMPILE_TARGETS` |
| `50-release` | official release build via `build_maho.py --release` into `out\Release` (`chrome`, `maho_mail_helper`, `mini_installer`); unsigned | steps 00-35 |
| `55-release-isolated` | official release build of `MAHO_RELEASE_REF` in its own tree `C:\rel` (own clone + own `gclient sync`, objects shared from `C:\src`), verified clean before and after | `MAHO_RELEASE_REF`, `local.bundle` |
| `60-release-bundle` | `release_local.py --skip-build --no-publish` from the step-55 tree: `MahoSetup-<v>.exe`, signed `update-win.json`, Store MSIX, `release-notes.html` | step 55, Sparkle seed |

Run order on a fresh machine: `00` → `10` → `15` → `30` → `35` → `40`.
`20` is only for verifying uncommitted work and can run any time after `10`.
Step 40 needs step 35 first: every Maho target that links Rust declares
`maho_ffi.lib` as an input.

Running a test binary after step 40 (example):

```powershell
setx MAHO_COMPILE_TARGETS "maho_agent_help_bar_test.exe maho_tab_borrow_chip_test.exe"
# run-detached.ps1 -Step 40-compile, then:
C:\src\chromium\src\out\Default\maho_agent_help_bar_test.exe
```

## Verifying uncommitted work

On the dev machine:

```bash
git bundle create local.bundle origin/main..main main   # unpushed commits
git diff --binary HEAD > overlay.patch                 # uncommitted changes
scp local.bundle overlay.patch maho-win:C:/src/maho-win-builder/
```

On the builder, `setx MAHO_REF local/main`, then run `10-checkout` (fast after
the first sync) and `20-overlay-worktree`. To compile single files, find their
object paths and pass them to step 40:

```powershell
cd C:\src\chromium\src
ninja -C out\Default -t targets all | findstr maho_unified_agent_adapter.obj
setx MAHO_COMPILE_TARGETS "obj/maho/browser/maho_unified_agent_adapter/maho_unified_agent_adapter.obj"
```

## sccache

`C:\src\maho-win-builder\sccache.env` holds `KEY=VALUE` lines for
`SCCACHE_BUCKET`, `SCCACHE_ENDPOINT`, `SCCACHE_REGION`, `SCCACHE_S3_KEY_PREFIX`,
`SCCACHE_S3_USE_SSL`, `AWS_ACCESS_KEY_ID` and `AWS_SECRET_ACCESS_KEY`. It is
never committed; copy the values from the existing sccache service config on
another builder. Step 30 persists them at User scope and fails if
`sccache --show-stats` does not report an `s3` cache location — a sccache
server started without them silently falls back to local disk.

## Build-time credentials

`maho_mail_ffi` bakes the Google OAuth desktop client into the binary, so step
35 needs `C:\src\maho-browser\.secrets\maho\google_desktop_oauth.json`
(or `MAHO_GOOGLE_CLIENT_SECRET_WINDOWS` / `GMAIL_CLIENT_SECRET` in the
environment). Release builds (step 50) also need
`.secrets\maho\google_chromium_api_key.env`. Copy it from the owner's vault with `scp`, restrict it with
`icacls C:\src\maho-browser\.secrets /inheritance:r /grant:r <user>:(OI)(CI)F`,
and never commit it: step 10 adds `.secrets/` to `.git/info/exclude`.

## Release builds must not share the verification tree

`C:\src\maho-browser` and `C:\src\chromium\src` are reused by every session
that verifies uncommitted work (step 20, ad-hoc resync jobs). A job that
resyncs or re-applies overrides there in the middle of a release build mixes
uncommitted changes into the release and races the build's own output
writes (`unable to rename temporary ... .obj.tmp`). Build releases with step
55, which owns `C:\rel` exclusively, and bundle them from that tree.

## Junctions and cleanup commands

`git clean` and `gclient sync -D` follow NTFS junctions. A junction from
`chromium\src\maho` into a checkout's `maho-chromium` therefore lets a clean of
the Chromium tree delete the checkout's sources. Step 55 unlinks its
junctions with `rmdir` (which removes only the link) before it cleans or
syncs, never passes `-D`, and resets the release checkout at the start of
every run.

## Pitfalls hit while bringing this up

| Symptom | Cause | Fix in the scripts |
|---------|-------|--------------------|
| `No supported Visual Studio can be found` | Chromium 154 requires VS 2026 (18.x); VS 2022 is not enough | step 00 installs VS 2026 Build Tools and sets `vs2026_install` |
| `include\10.0.28000.0\um ... does not exist` | Chromium 154 pins Windows SDK 10.0.28000 | step 00 adds `Windows11SDK.28000` |
| VS installer exits 1, log says `--installPath C:\Program` | `Start-Process` joins arguments with spaces | install path is passed quoted |
| bootstrapper "file is corrupt" | `aka.ms/vs/18/release` redirects to Bing HTML | uses `aka.ms/vs/18/stable` and checks the Authenticode signer |
| `vswhere -requires ...ATLMFC` succeeds but `atlbase.h` is missing | vswhere reports the workload, not the optional component | step 00 checks the header on disk |
| `gclient sync` fails: "file contains a virus or potentially unwanted software" | Defender quarantines the CIPD package `chromium/third_party/updater/chrome_win_x86` | Defender exclusion for `C:\src` (remove with `Remove-MpPreference -ExclusionPath C:\src`) |
| `invalid path 'maho/crates/maho-ffi/:memory:/maho.db'` | the repo tracks test artifacts under a name NTFS cannot hold | sparse-checkout excludes the directory and `core.protectNTFS=false` (index only) |
| `git apply` rejects every hunk | machine-wide `core.autocrlf=true` writes CRLF | the clone sets `core.autocrlf=false` |
| step 35: `GMAIL_CLIENT_SECRET is required` | Mail FFI needs the OAuth client at build time | provide `.secrets/maho/google_desktop_oauth.json` (see above) |
| step 35: `needs llvm-nm/llvm-ar/llvm-objcopy` | staticlib symbol privatization uses rustup llvm-tools | step 35 runs `rustup component add llvm-tools` |
| step 35: `FileNotFoundError` after `Generating maho_ffi.h using cbindgen` | cbindgen not installed | step 35 installs the pinned cbindgen |
| `'../../../../maho/version.txt' ... missing` | GN expects `maho/` two levels above `chromium/src` | step 10 creates the `C:\src\maho` junction |
| WebUI actions fail: `Cannot find module ...website\node_modules\esbuild` / tailwind `NotADirectoryError` | Maho WebUI GN actions import from `<workspace>/website/node_modules`, which only the private workspace has | step 15 installs it |
| WebUI bundles: `Could not resolve "sonner"` / `"codemirror"`, tailwind `Can't resolve 'tailwindcss'` | the WebUI imports packages (sonner, marked, codemirror + @codemirror/*, six @radix-ui primitives, tw-animate-css, @tailwindcss/cli, esbuild) that `website/package.json` does not declare; dev Macs have them from ad hoc installs | step 15 pins the reference-Mac versions; follow-up: declare them in a package |
| step 15: `lockfile had changes, but lockfile is frozen` | bun re-resolves Windows-only optional packages in a macOS-written lock | step 15 installs without `--frozen-lockfile` |
| `maho_ffi.lib ... missing` right after an overlay | `git clean` in step 20 deleted untracked build products | step 20 excludes `prebuilt/`, `website`, `node_modules` and `.secrets` from the clean |
| step 50: `base patch does not apply to ...` after verifying an uncommitted overlay | `chromium\src` still carries the overlay's version of a base patch; `apply_chromium_src_overrides.py` only accepts pristine or exactly-patched files | `git -C C:\src\chromium\src checkout -- <file>` for that file, then `--dry-run` must exit 0 |
| `can't open file ...\maho-chromium\build\scripts\build_maho.py` in `C:\rel` | `gclient sync -D` / `git clean` recursed through the `src\maho` junction and deleted the release checkout's `maho-chromium` | step 55 unlinks junctions first, drops `-D`, resets the checkout every run |
| clang compiles never hit sccache (`Non-cacheable: -fmodules-cache-path`) | Chromium 154 enables explicit Clang modules by default; sccache cannot cache module compiles | every args template sets `use_clang_modules = false` |
| step reports failure on success output | PowerShell 5 turns native stderr into a terminating error under `Stop` | scripts use `Continue` and check `$LASTEXITCODE` |
