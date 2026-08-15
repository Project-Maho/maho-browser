# Chromium desktop macOS E2E

Chromium desktop validation has two distinct surfaces:

1. The approved isolated macOS VM contract, whose checked-in CLI exposes
   `doctor`, `prepare`, `list`, and `run`. Preflight, preparation, and
   inventory receipts are captured; the live `run all` proof is pending.
2. The existing AI panel CDP harness, which can be used locally and is the
   guest-local executor for the VM `ai-mock` suite.

The historical Accessibility sidebar runner is unavailable. Do not turn its
commands into executable instructions or use them as desktop proof.
For Chromium overlay build prerequisites, see `docs/chromium-fork-build.md`.
For the normative VM lifecycle and privacy/artifact/failure contracts, see the
workspace-level `docs/operations/macos-vm-e2e.md`.

## VM lifecycle CLI

Run from the workspace root:

```bash
TART_HOME=/Volumes/maho-build/maho-tart \
  python3 maho-chromium/build/scripts/macos_vm_e2e.py doctor \
  --state-dir /Volumes/maho-build/maho-vm-state \
  --identity ~/.cache/maho-macos-vm/id_ed25519
TART_HOME=/Volumes/maho-build/maho-tart \
  python3 maho-chromium/build/scripts/macos_vm_e2e.py prepare \
  --state-dir /Volumes/maho-build/maho-vm-state \
  --identity ~/.cache/maho-macos-vm/id_ed25519
python3 maho-chromium/build/scripts/macos_vm_e2e.py list --json
TART_HOME=/Volumes/maho-build/maho-tart \
  python3 maho-chromium/build/scripts/macos_vm_e2e.py run \
  --suite all \
  --golden maho-e2e-golden \
  --identity ~/.cache/maho-macos-vm/id_ed25519 \
  --state-dir /Volumes/maho-build/maho-vm-state
```

This workstation stores Tart's images and the CLI's ownership state on the
external `/Volumes/maho-build` volume. Tart inherits `TART_HOME` from the
environment; `--state-dir` (or `MAHO_MACOS_VM_STATE`) governs Maho's ownership
state instead. Pass both to `doctor`, `prepare`, and `run`. `list` reads only
the checked-in manifest and accepts neither. The SSH identity stays at
`~/.cache/maho-macos-vm/id_ed25519`.

`doctor` reports host readiness, `prepare` creates or reuses the owned golden
image, `list` validates `macos_vm_e2e_manifest.json` and prints its inventory,
and `run` accepts `interactive-smoke`, `ai-mock`, or `all`. Those first three
have reviewed receipts in the workspace-level runbook; a receipt for one clean
`all` run remains pending there. When a wrapper misbehaves, note that `--help`
exits `0` and an unknown `run --suite` value exits `2` before any VM is
created.

## VM contract summary

- The golden image is `maho-e2e-golden`, derived from the approved base image.
- The host workspace is shared read-only. The guest sees it at
  `/Volumes/My Shared Files/maho-workspace` and remounts it read-only at
  `/Users/Shared/maho-workspace`; the fallback copies only declared
  runtime dependencies and suite scripts to that exact path.
- Chromium builds only in the dedicated host output `chromium/src/out/VM-E2E`,
  which the guest reads through the share; `out/Default` is not reused.
- CDP and the AI mock endpoint bind to loopback.
- Every suite receives a fresh profile, and disposable profiles and VM disks are
  deleted rather than exported.
- Required proof includes the validated manifest, a machine-readable run
  summary, provisioning/build/suite logs, gtest results, AI scenario results,
  bounded failure diagnostics, and teardown status.
- A missing build, launch failure, timeout, artifact-export failure, or cleanup
  failure is an infrastructure error, not a pass. Test failures are not retried
  automatically.

The approved VM suite never imports a host Maho profile or real provider key.
Mobile Appium remains a separate iOS/Android test layer and is not run inside
the desktop guest.

## AI panel harness (chrome://maho-ai)

The AI panel is a WebUI (React), so its local harness drives it over the Chrome
DevTools Protocol for DOM inspection and text entry:

```bash
node maho-chromium/build/scripts/e2e_chromium_ai.js --list
node maho-chromium/build/scripts/e2e_chromium_ai.js                       # launch, run all, exit
node maho-chromium/build/scripts/e2e_chromium_ai.js --scenario ai_real_turn
node maho-chromium/build/scripts/e2e_chromium_ai.js --serve               # keep a persistent instance alive
MAHO_E2E_CDP_PORT=9422 node maho-chromium/build/scripts/e2e_chromium_ai.js --attach --scenario ai_panel_renders
```

Outside the VM contract, the harness can seed a temporary profile with the
`maho.ai` provider config read from a real profile or environment overrides.
That is a local developer mode, not approved isolated proof. The VM `ai-mock`
suite must use `--mock`, a fresh guest profile, loopback CDP, and no real
credentials. The harness uses Node's built-in `WebSocket` and needs no extra
package dependency.

Scenarios:

- `ai_panel_renders` — asserts the AI WebUI mounts (title, prompt input, DOM).
  Deterministic; needs no LLM.
- `ai_real_turn` — types a marker prompt, submits, and asserts the model echoes
  the marker. Runs a real agent turn end-to-end (WebUI → Mojo → adapter → FFI →
  Rust backend → configured LLM endpoint), so it requires a reachable provider.

Env:

| Variable | Default | Purpose |
| --- | --- | --- |
| `MAHO_APP_PATH` | `chromium/src/out/Default/Maho.app` | App bundle to launch. |
| `MAHO_E2E_SOURCE_PROFILE` | `~/Library/Application Support/Maho` | Profile whose `maho.ai.*` prefs seed the temp profile. |
| `MAHO_E2E_AI_PROVIDER` / `_API_KEY` / `_BASE_URL` / `_MODEL` | from source profile | Explicit provider overrides. |
| `MAHO_E2E_CDP_PORT` | `9422` | Remote debugging port. |
| `MAHO_E2E_KEEP_PROFILE=1` | unset | Keep the temp profile after the run. |

### Session-independent testing (`--serve` / `--attach`)

Other dev sessions constantly rebuild `chromium/src/out/Default` and may kill
the app. Two properties make testing robust against that churn:

1. **A running instance survives `out/Default` file churn.** A macOS process
   keeps its `mmap`'d dylib inodes even after the files on disk are relinked by
   a rebuild. So start a long-lived instance with `--serve` (it prints
   `CDP_PORT=`, `PID=`, `PROFILE=`, then stays up until `SIGINT`/`SIGTERM`), and
   drive it repeatedly with `--attach` — each `--attach` run connects to the
   existing `MAHO_E2E_CDP_PORT` instead of launching. The served instance keeps
   working across rebuilds of the very directory it was launched from.

2. **Relocating the app does NOT work.** The dev build is a component build:
   `Maho.app` is not self-contained — its main binary and helpers resolve ~530
   sibling dylibs from the `out/Default` top level via rpath. Copying the bundle
   plus the dylibs elsewhere (even with `codesign --force --deep`) produces an
   app that binds its debugging port but hangs during init and never serves
   `/json`. There is therefore no supported way to freeze a component build off
   `out/Default`; do not attempt to snapshot/clone the runtime set.

The `--serve` / `--attach` model on `out/Default` is the primary mechanism and
needs no extra build. Note that the AI panel is a WebUI: with `load_webui_from_disk`
enabled in the dev `args.gn`, changing the panel's TypeScript/React does **not**
require a Chromium rebuild — rebuild the WebUI bundle and reload the served
instance. Only C++/Rust/Mojo changes need a Chromium rebuild (which rebuilds the
shared `out/Default`).

#### Optional: a binary immune to churn even at rest (`out/E2E`)

For a binary that is immune to `out/Default` rebuilds **at rest** (not just while
running), build into a dedicated output directory and point `MAHO_APP_PATH` at it:

```bash
python3 maho-chromium/build/scripts/build_maho.py --out-dir out/E2E --no-launch
MAHO_APP_PATH=$PWD/chromium/src/out/E2E/Maho.app \
  node maho-chromium/build/scripts/e2e_chromium_ai.js --serve
```

**This is a one-time FULL build (hours).** There is no compiler cache configured
(no ccache/sccache/reclient in `args.gn`), so a fresh out directory shares no
object files with `out/Default` and recompiles everything. This path is therefore
only worthwhile to **pin one binary for a sustained QA campaign** — build once,
then test for days while other sessions churn `out/Default` — and is **not**
suitable for fast iterate-and-test loops.

Do not clone or manually regenerate an output tree as a shortcut. Unverified raw
GN or Ninja commands are not supported here; use `build_maho.py` so build locking,
arguments, and output ownership remain canonical.

**Caveat:** another session that terminates every process named Maho can still stop
a served instance. Do not use broad process-name termination; stop only a PID your
session owns, and restart `--serve` if an external session interrupts it.

### Self-test

`maho-chromium/build/scripts/e2e_isolate_selftest.sh` proves the mechanics
against a rendered `out/Default`:

```bash
maho-chromium/build/scripts/e2e_isolate_selftest.sh
MAHO_E2E_CHURN_TEST=1 maho-chromium/build/scripts/e2e_isolate_selftest.sh
```

It runs `S1` (serve + attach render), `S3` (two concurrent `--serve` instances
on distinct ports, both attachable), and `S4` (default one-shot mode). `S2`
(inode-swap survival) is **off by default** because it mutates the shared
`out/Default`; enable it with `MAHO_E2E_CHURN_TEST=1` only when no concurrent
build holds `.build.lock`.

Not yet covered (require MahoCore workspace/MCP seeding to expose a sensitive
tool, plus a stable window-frame/theme layer): permission-dialog allow/deny,
`approval_policy=deny` tool blocking, `profile.tools`/`mcp_servers` scoping, and
MCP liveness — these are covered at the unit/integration level today.
