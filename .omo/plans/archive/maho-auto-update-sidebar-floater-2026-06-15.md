# Maho Sidebar Footer Update Pill — Wiring Plan

**Created**: 2026-06-15 JST
**Source**: Plan §F4.3 deferred from `.omo/plans/maho-auto-update-browser-relay-wiring-plan-2026-06-15.md`
**Predecessor work** (already in working tree, not committed):
- `MahoUpdateManager` + `MahoUpdateObserver` interface — emits `OnUpdateStateChanged(UpdateState)` and `OnUpdateProgress(double)`. Currently zero subscribers.
- `MahoSidebarFooterView::update_pill_` UI — already laid out: rounded surface + indicator dot + label "Maho is up to date". Calls `UpdateUpdatePill(MahoSidebarFooterUpdatePillModel)` to render.
- `MahoSidebarFooterUpdatePillModel` data struct: `{visible, has_update, label, accessible_label}`.
- `MahoSidebarStateAdapter::BuildFooterModel()` — currently hard-codes the pill to invisible (lines 977–980).
**Status**: Plan-only. No code written for this plan yet.
**Scope**: Wire the existing sidebar footer pill to `MahoUpdateManager` state so the user sees a real "An update is available — Restart" pill at the bottom of the sidebar. No new view classes, no new layouts — the pill widget exists; only data flow + click handling are missing.

---

## 0. Goal

When the user is on a Maho build with auto-update enabled and the relay returns a manifest matching `version > current`, the sidebar footer pill:

1. Shows the indicator dot in primary color.
2. Shows the label "An update is available — Restart" (or equivalent localized text).
3. Becomes clickable. Click invokes `MahoUpdateManager::ApplyUpdateAndRestart()`.
4. Hides itself when state is `kIdle` or `kUpToDate` (current default behavior).
5. Shows a non-clickable "Checking for updates…" (with spinner-like indicator) while `kChecking` / `kDownloading` / `kVerifying`.
6. Shows a non-clickable "Update failed — retry" if state is `kError(non-crash-loop)`. Click triggers a new `CheckForUpdates(manual=true)`.

Behind the scenes: a `base::OneShotTimer` in `MahoUpdateManager` re-fires `CheckForUpdates(manual=false)` every 4 hours so the user does not need to manually trigger checks.

### Non-goals (deferred)
- Settings UI for "Check for updates" button + channel selector (Plan F4.1, F4.2). The pill is enough for "user sees update is available" — explicit menu surface comes later.
- Animations / progress bar / percent text.
- macOS Sparkle-driven UI (Sparkle 2 already shows its own dialog when `SUEnableAutomaticChecks=YES`; we keep `SUEnableAutomaticChecks=NO` per the existing Info.plist patch and drive everything ourselves).
- Toolbar / menu badge (Plan F4.3 mentioned this; pill alone is enough for now).
- Any visual A/B test or rollout gating of the pill.

### Success criteria (binary)
- [ ] `bun run typecheck` / `bun run test` clean (Rust workspace untouched, but verifying nothing breaks via root proxies).
- [ ] `python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target unit_tests` succeeds.
- [ ] `python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target chrome` succeeds.
- [ ] Existing 27 `MahoUpdate*Test` / `SignatureTest` cases pass.
- [ ] New tests: at least 4 unit tests covering pill state transitions (idle, checking, available, error). All pass.
- [ ] Existing sidebar tests pass (no regression in `MahoSidebar*Test`).
- [ ] Manual smoke on macOS dev host with `MAHO_UPDATE_SERVER_OVERRIDE=http://localhost:18080` + kill-switch on + fresh fixture release: pill appears in sidebar footer with "Restart" copy, click triggers `ApplyUpdateAndRestart()`.

---

## 1. Architecture Snapshot — How the Pieces Connect

```
┌──────────────────────────────────────────────────────────────────┐
│  MahoUpdateManager  (process singleton)                          │
│   - state_, last_error_                                          │
│   - kCheckIntervalMinutes timer (4h)  ← NEW                      │
│   - emits OnUpdateStateChanged(state) to observers               │
└─────────────────────────────┬────────────────────────────────────┘
                              │ observer pattern
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│  MahoSidebarUpdatePillController  ← NEW (per-Browser)            │
│   - implements MahoUpdateObserver                                │
│   - holds reference to MahoSidebarFooterView (or notifies        │
│     MahoSidebarStateAdapter to rebuild)                          │
│   - hops to UI thread, then invokes adapter.RebuildFooter()      │
└─────────────────────────────┬────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│  MahoSidebarStateAdapter::BuildFooterModel()                     │
│   - reads MahoUpdateManager state + last_error                   │
│   - populates model.update_pill { visible, has_update, label,    │
│     accessible_label }                                           │
└─────────────────────────────┬────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│  MahoSidebarFooterView::UpdateUpdatePill(model)                  │
│   - renders the existing update_pill_ widget                     │
│   - on click → controller.OnPillActivated(state)                 │
└──────────────────────────────────────────────────────────────────┘
```

### Click semantics (driven by current state)

| State | Pill visible? | Indicator color | Click action |
|---|---|---|---|
| `kIdle` | No | – | – |
| `kChecking` / `kDownloading` / `kVerifying` | Yes (no click) | Subtle | – (cursor: default, not pointer) |
| `kUpdateAvailable` | Yes | Primary (pulsing optional, deferred) | Trigger `CheckForUpdates(manual=true)` to advance to download |
| `kReadyToInstall` | Yes | Primary | `ApplyUpdateAndRestart()` |
| `kUpToDate` | No | – | – |
| `kError(kCrashLoop)` | Yes (non-clickable) | Warning | – (display only, user can't retry from here) |
| `kError(other)` | Yes | Warning | `CheckForUpdates(manual=true)` to retry |

### Auto-check cadence

- A `base::OneShotTimer` started in `MahoUpdateManager::Initialize()` fires `CheckForUpdates(manual=false)` after `kInitialDelay = 30s` (avoid contending with browser startup), then re-arms for `kCheckIntervalMinutes = 240` (4h).
- Timer is cancelled in dtor and on `is_disabled_` / crash-loop trip.
- `kCheckIntervalMinutes` is a `constexpr` for now; making it a pref is deferred to F4.

---

## 2. Phasing

Phases ship as one atomic PR — they're tightly coupled to a single user-visible feature. Tasks marked `‖` may be parallelized within the PR.

### Phase G1 — Pill controller + state-adapter wiring

- **G1.1** New file `maho-chromium/browser/ui/views/sidebar/maho_sidebar_update_pill_controller.{cc,h}`:
  - Class `MahoSidebarUpdatePillController : public MahoUpdateObserver`.
  - Constructor takes `Browser*` and `MahoSidebarStateAdapter*` (weak).
  - In ctor: register self with `MahoUpdateManager::GetInstance()->AddObserver(this)`.
  - In dtor: `RemoveObserver`.
  - `OnUpdateStateChanged(state)` → posts task to UI thread → calls back into `MahoSidebarFooterView::Update(adapter->BuildFooterModel(browser))` (via the adapter's existing notify mechanism).
  - Public `OnPillActivated()` invoked by the footer view when the pill is clicked.

- **G1.2** `MahoSidebarStateAdapter::BuildFooterModel()` (`maho_sidebar_state_adapter.cc:977-980`) reads `MahoUpdateManager`:
  - If disabled / crash-loop: pill invisible.
  - Else: map `state` → `(visible, has_update, label, accessible_label)` per the table above.
  - Localized strings for now use hardcoded English (i18n is its own workstream); add a `// TODO(maho-i18n)` for resource-string migration.

- **G1.3** `MahoSidebarFooterView`:
  - Replace the static `update_pill_` View with a `views::Button` (or wrap with mouse handler) so click events propagate.
  - Add an `OnUpdatePillPressed()` private method that calls into a `MahoSidebarUpdatePillController*` (member injected by sidebar view's owner, or via `Browser->GetUserData()`-style attachment).
  - Cursor: pointer when state is clickable, default otherwise.

- **G1.4** `MahoSidebarView`:
  - Owns `std::unique_ptr<MahoSidebarUpdatePillController>` per Browser.
  - Wires the controller into the footer view at construction.

### Phase G2 — Auto-check timer

- **G2.1** `MahoUpdateManager`:
  - New private members `base::OneShotTimer auto_check_timer_;` + `constexpr base::TimeDelta kInitialAutoCheckDelay = base::Seconds(30);` + `constexpr base::TimeDelta kAutoCheckInterval = base::Hours(4);`.
  - In `Initialize()` (after kill-switch + crash-loop checks pass): start timer with `kInitialAutoCheckDelay`, callback runs `CheckForUpdates(manual=false)` and re-arms with `kAutoCheckInterval`.
  - In dtor / `ResetForTesting()`: stop timer.
  - In `Initialize()` if disabled/crash-loop: do NOT start timer.

- **G2.2** Manager exposes `set_auto_check_interval_for_testing(base::TimeDelta)` so unit tests can dial the interval down without `base::test::TaskEnvironment::FastForwardBy(4h)`.

### Phase G3 — Tests

- **G3.1** `MahoSidebarUpdatePillControllerTest` (new file):
  - `TransitionsAreObserved` — fake delegate transitions through states; controller receives each `OnUpdateStateChanged`.
  - `RebuildsFooterOnStateChange` — mocked adapter; expect adapter callback fired once per state change.
  - `RemovesItselfOnDestruction` — manager's observer count returns to 0 after controller drops.

- **G3.2** Extend `MahoUpdateManagerTest`:
  - `AutoCheckTimer_FiresAfterInitialDelay` — `Initialize()`, fast-forward by `kInitialAutoCheckDelay`, expect `delegate->check_count == 1`.
  - `AutoCheckTimer_StoppedWhenDisabled` — kill-switch on; `Initialize()`; fast-forward 8h; expect `delegate->check_count == 0`.
  - `AutoCheckTimer_StoppedOnCrashLoop` — sentinel tripped; `Initialize()`; expect timer never started.

- **G3.3** State adapter unit test (probably new file `maho_sidebar_state_adapter_unittest.cc` or extending an existing one if one exists):
  - For each `UpdateState`, `BuildFooterModel` returns the expected pill model.

### Phase G4 — Visual smoke

- **G4.1** Manual: dev runs the `update_e2e_smoke.sh` + `install_dev_update_key.sh` + kill-switch flip + `MAHO_UPDATE_SERVER_OVERRIDE=http://localhost:18080`, launches Maho.app, manually invokes `CheckForUpdates(true)` (or waits 30s for auto-check), confirms pill appears. Click and confirm `ApplyUpdateAndRestart()` is invoked (process exits to relaunch).
- **G4.2** Screenshot or terminal dump captured in PR description (no automated visual QA in this plan; that's its own workstream).

---

## 3. File-by-File Summary

```
# Phase G1
maho-chromium/browser/ui/views/sidebar/maho_sidebar_update_pill_controller.{cc,h}  신규
maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc                (replace lines 977-980 with manager query)
maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc                  (pill becomes Button; OnUpdatePillPressed handler)
maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.h                   (new pill click callback wiring)
maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.{cc,h}                     (owns controller)

# Phase G2
maho-chromium/browser/updates/maho_update_manager.{cc,h}                            (auto_check_timer_, kAutoCheckInterval)

# Phase G3
maho-chromium/browser/ui/views/sidebar/maho_sidebar_update_pill_controller_unittest.cc  신규
maho-chromium/browser/updates/maho_update_manager_unittest.cc                       (+3 timer tests)
maho-chromium/browser/ui/views/sidebar/BUILD.gn                                      (+ controller sources)
maho-chromium/browser/BUILD.gn                                                       (+ test source if separate target)
```

**Total**: 1 PR, ~10 files touched (3 new, 7 modified), no new external deps.

---

## 4. Verification Plan

```sh
# Type / Rust workspace sanity
bun run typecheck
bun run test

# Build narrow target
python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target unit_tests

# Run new + existing update tests + sidebar tests
chromium/src/out/Default/unit_tests \
    --gtest_filter='MahoUpdate*:Signature*:MahoSidebarUpdatePillControllerTest.*:MahoSidebarStateAdapter*' \
    --test-launcher-jobs=1

# Full chrome target
python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target chrome

# Manual smoke (macOS host, dev environment)
maho-chromium/build/scripts/update_e2e_smoke.sh
maho-chromium/build/scripts/install_dev_update_key.sh \
    --private $STATE_DIR/private.bin --public $STATE_DIR/public.bin
python3 maho-chromium/build/scripts/build_maho.py --ninja-target chrome
# Edit ~/Library/Application Support/Maho/Local State to set
#   "maho":{"update":{"enabled":true}}
MAHO_UPDATE_SERVER_OVERRIDE=http://localhost:18080 \
    chromium/src/out/Default/Maho.app/Contents/MacOS/Maho
# After 30s the pill should appear with "An update is available — Restart"
```

Per-PR exit criteria: all build/test commands exit 0; new pill controller tests + auto-check timer tests + state-adapter pill tests pass; existing tests do not regress.

---

## 5. Risks & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Observer registered on UI thread, manager fires on a posted task → race in dtor | Med | Med | Controller dtor must `RemoveObserver` synchronously on UI thread; observer list is `base::ObserverList<MahoUpdateObserver>` (thread-affine to manager's sequence). Document with `DCHECK_CALLED_ON_VALID_SEQUENCE`. |
| State adapter rebuild storm if observer fires on every progress tick | Low | Med | `OnUpdateProgress` is NOT used to trigger rebuilds; only `OnUpdateStateChanged`. Progress is for future progress bar UI. |
| Click on non-clickable state (e.g. `kChecking`) | Low | Low | `MahoSidebarFooterView` checks current model's `clickable` flag (new field on model) before propagating click; Button is in disabled state otherwise. |
| Auto-check timer fires during shutdown / browser closing | Low | Med | `MahoUpdateManager` is process-singleton; lifetime tied to browser process. Timer is on UI thread, so all fires are well-ordered with shutdown. |
| Pill text triggers layout reflow on every state change | Low | Low | `MahoSidebarFooterView::Update` is the existing rebuild path; we're already paying its cost. |
| Hardcoded English copy in v1 | Med | Low | Explicit `// TODO(maho-i18n)` flagged; resource-string migration is a separate workstream. |

---

## 6. Open Questions (to resolve before G1 starts)

1. **Click target: Button vs. wrapper view with mouse handler?** Recommend `views::Button` since it ships with a11y + focus + key activation + cursor change for free.
2. **Should the pill remain visible during `kUpToDate`?** Plan says no (matches the existing `Maho is up to date` placeholder behavior — which sets `visible=false` per the current model). Keeps the footer clean. Could revisit if telemetry shows users want feedback after a manual check.
3. **Auto-check interval tuning?** Plan picks 4h matching Chrome stable's update check cadence. Should this be per-channel (canary 30min, stable 4h)? Out of scope; revisit when channel selector lands.
4. **Should disabled state (`kError(kCrashLoop)`) show different copy ("Update disabled — see logs")?** Recommend yes; falls inside G1.2 string mapping.

---

## 7. Self-Rollout Plan

The pill itself does NOT change the kill-switch default. Behavior:

1. G1+G2+G3 land → pill code is built, observer registered, timer starts. **But kill-switch is still `false` by default.** With kill-switch off, `Initialize()` early-returns BEFORE auto-check timer starts, BEFORE observer registration → pill never becomes visible. Safe.
2. Internal cohort flips `maho.update.enabled=true` (manually or via a future settings UI). Pill activates.
3. After at least one full cycle of `kCheckIntervalMinutes` against staging server with no incidents, the kill-switch default flips to `true` for stable channel — separate PR, separate decision.

The pill is the *feedback mechanism* for staged rollout; it does not itself bypass any of the existing safety gates.

---

## 8. Out-of-Scope Follow-ups (after G1+G2+G3)

| Item | Type |
|---|---|
| Per-channel auto-check interval | Settings UI workstream |
| Toolbar / menu "Restart" badge | Plan F4.3 (deferred) |
| `chrome://maho-settings#updates` surface | Plan F4.1 |
| Animated indicator / progress bar | Visual polish workstream |
| Localized resource strings | i18n workstream |
| Telemetry on pill clicks (`Maho.Update.PillClick`) | Analytics workstream |
