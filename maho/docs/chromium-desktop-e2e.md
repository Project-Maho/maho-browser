# Chromium desktop E2E

> **2026-09-18:** VM 기반 데스크톱 E2E는 폐기되어 완전 삭제되었습니다(VM 테스트 전면 금지,
> 사용자 지시 — VM 러너 코드·매니페스트·VM 전용 런북 포함). 데스크톱 E2E/검증은
> omarchy(100.91.254.71)에서 실행합니다. 이 문서는 VM과 무관한 로컬 AI 패널 CDP 하네스만
> 다룹니다.

The historical Accessibility sidebar runner is unavailable. Do not turn its
commands into executable instructions or use them as desktop proof.
For Chromium overlay build prerequisites, see `docs/chromium-fork-build.md`.

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

The harness can seed a temporary profile with the `maho.ai` provider config
read from a real profile or environment overrides. That is a local developer
mode, not isolated proof. Mock mode (`--mock`) must use a fresh profile,
loopback CDP, and no real credentials. The harness uses Node's built-in
`WebSocket` and needs no extra package dependency.

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
