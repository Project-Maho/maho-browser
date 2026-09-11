# Baseline-only Tart harness

`hybrid-visual` currently admits only `baseline`. It uses the existing browser
and one persistent `maho browser pipe` process, not CDP, native input, or a model.
OS-synthesized input is not physical hardware input or proof of human behavior.

Run from `/Users/indo/code/project/maho-workspace` on the host:

```sh
export PATH=/opt/homebrew/bin:$PATH
python3 maho/scripts/build_lock.py -- cargo build --manifest-path maho/Cargo.toml -p maho-cli --release
python3 maho-chromium/build/scripts/hybrid_visual_pipeline.py stage-cli --workspace "$PWD"
TART_HOME=/Volumes/T9-Mac/maho-tart python3 maho-chromium/build/scripts/macos_vm_e2e.py run --suite hybrid-visual --golden maho-e2e-golden --identity /Users/indo/.cache/maho-macos-vm/id_ed25519 --state-dir /Volumes/T9-Mac/maho-vm-state --skip-build --require-transport virtiofs-read-only
```

The runner reuses host builds. After browser changes, rebuild through the
canonical lock-backed wrapper before staging. CLI staging creates an owned
`chromium/src/out/Default/hybrid-baseline/Maho.app` copy and places the host
release CLI at `Contents/Helpers/maho` so browser peer authentication recognizes
the bundled controller. The original `Maho.app` is preserved. Staging records
CLI, browser and component provenance; an old bundle is not proof of the latest
host build. Host and guest preflight both check staged hashes. Tart uses the
existing read-only workspace/Chromium shares. The baseline refuses fallback to
writable transport. The guest must already have `/opt/homebrew/bin/python3`
version 3.10 or newer; preflight fails explicitly otherwise. The runner does not
install Python or build anything inside the guest.

The dedicated loopback HTTP fixture has no TLS bypass or remote assets. Its
server-listening completion precedes browser launch. Page readiness is a POST
receipt subscribed before launch; the CLI starts only after the existing MCP
listening log event. Each click/type receipt is subscribed before the request.
The fixture verifies the resulting click count or replaced field value and
records `isTrusted`. `verified=dispatched` alone is never accepted as receipt.
The disposable profile has no competing controller. Requests explicitly use
the existing scoped lease behavior, which can inherit internal-CLI acquisition
policy; this is not a lease-contention or authorization regression test. No
implicit origin grant is issued. Browser denials remain failures, not bypasses.

The three operation groups are snapshot observe, act click, and act type, with
50 raw samples each. `elapsed_ms` ends at the RPC response and is **RPC latency**,
not end-to-end input completion latency. Receipt completion is separately awaited
before proceeding. P50 is the arithmetic mean of sorted samples 25 and 26; P95
uses nearest-rank sample 48. Raw responses, transcript, page receipts and hashes
are included in `baseline-latency.json`.

`MEASURED` means the harness measurement/receipt contract was met, not full item
1a or F4 performance acceptance. Internal browser screenshot/model invocation
counts are **null/unverified**, distinct from counts of driver-issued operations.
The fixture's ready receipt supplies screen and viewport dimensions in CSS
pixels and `devicePixelRatio`; missing or invalid display metadata is rejected.
Each input receipt also captures the current dimensions, preserving viewport
changes caused by the control-activity panel during measurement.
Unobserved internal invocation counts still block a full
comparable baseline acceptance claim. No native/model/hybrid scenario is enabled.

The driver terminates only its own child processes and closes its fixture,
including when artifact creation fails. The existing runner owns disposable VM
cleanup. Runtime proof of launch, guest Python availability, authorization,
delivery, cleanup and 50-sample measurements remains the capture node's job.

## StealthSuite Taxonomy and Guest Execution Path

In addition to `hybrid-visual` (baseline-only), the Tart runner supports `StealthSuite`
with kind `stealth`:

- **Suites**:
  - `stealth-local`: Comprehensive local scenarios (`sentinel`, `coordinates`, `permission-denied`, `native-input`, `security`, `model-loop`, `recovery`, `fingerprint`, `challenge`, `misclick`, `telemetry`).
  - `stealth-turnstile-online`: Cloudflare Turnstile integration testing via force-interactive test sitekey (`turnstile-online`).
  - `stealth-provider-online`: Real vision provider validation (`model-provider-online`).
  - `stealth-offline`: Offline aggregate suite covering `stealth-local`.

- **Guest Invocation**:
  The driver executes via the fixed entry point:
  ```sh
  /opt/homebrew/bin/python3 maho-chromium/build/scripts/hybrid_visual_pipeline.py run \
    --scenario <scenario-id> \
    --cli <cli-path> \
    --app <app-path> \
    --evidence-dir <evidence-dir> \
    --output <summary-json-path>
  ```
  The runner dispatches the command, validates receipts via `validate_stealth_result`,
  and exports standardized artifacts (`cleanup.json`, `transcript.json`, `fixture-receipts.json`,
  `browser.stderr.log`, `cli.stderr.log`, `<suite>-summary.json`).

## Availability and errors

Native input synthesis preflight and error reporting operate under strict honesty principles:

1. **Preflight Result Contract**:
   - Availability is returned as `{available, reason, raw OS status}` (`NativeInputAvailability`).
   - Platform probes execute read-only capability checks without mutating system permission state or prompting interactive consent.

2. **Per-Platform Probes**:
   - **macOS**: Queries `AXIsProcessTrustedWithOptions(nullptr)`. The dictionary argument is explicitly `nullptr` to ensure the operating system suppresses user prompts. An affirmative result establishes that the calling process is trusted by Accessibility services; it is **never** claimed that accessibility trust proves event delivery.
   - **Windows**: Checks for an interactive window station and desktop context (`WSF_VISIBLE` flag on `GetProcessWindowStation()`). Insertion failures from `SendInput` (zero events inserted) are reported honestly with their raw OS error codes; an insertion failure is **never** claimed to prove a UIPI cause, as Windows returns ambiguous error statuses that may stem from desktop restrictions, invalid input structure, or UIPI.
   - **Linux**: Returns `available = false` and `raw_os_status = 0` as native OS input synthesis is unsupported.

3. **Machine Error Code -32011 (`kMahoMcpErrorNativeInputUnavailable`)**:
   - Allocated code `-32011` is dedicated exclusively to native input unavailability.
   - Returned whenever native input is required (such as during active hybrid visual fallback `visual_active` or an explicit native-mode request) but the preflight probe determines native input is unavailable.
   - The JSON-RPC error body embeds `{available: false, reason: <string>, raw_os_status: <int>}` in `error.data`, providing machine-actionable diagnostic context while strictly adhering to honest reporting bounds.

