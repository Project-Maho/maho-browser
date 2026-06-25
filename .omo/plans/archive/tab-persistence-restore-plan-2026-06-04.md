# Tab Persistence & Restore Plan — 2026-06-04

## Status
**Diagnosis: COMPLETE** (3 parallel agents + Oracle review)
**Implementation: NOT STARTED**

---

## Problem Statement

User opens tabs in Maho → Cmd+Q → relaunch → tabs gone, only `chrome://newtab` shown.
Even after seeding 9 tabs/folders/spaces directly into LMDB via `seed_initial.rs`, the sidebar shows only a fresh NTP. Persisted tab metadata exists in `tab_manager` (LMDB) but no `WebContents` ever spawn for them.

The previous fix (`MahoTabRegistry::OnTabStripModelChanged(kRemoved)` returns early when `IsTryingToQuit()`) successfully prevented the LMDB from being nuked on quit — but exposed that **session restore was never running in the first place**.

---

## Root Cause: 3 Compounding Failures (each independently sufficient to break restore)

### Layer 1 (DECISIVE) — `exit_type = "Crashed"` blocks all restore logic
- `~/Library/Application Support/Maho/Default/Preferences` has `profile.exit_type = "Crashed"`. The `exited_cleanly` key is absent — Maho has never completed a clean shutdown that wrote a normal exit type.
- `startup_browser_creator_impl.cc:455` sets `is_post_crash_launch = HasPendingUncleanExit(profile_) = true`.
- `DetermineStartupTabs()` at line 596-603 then short-circuits:
  ```cpp
  if (is_incognito_or_guest || is_post_crash_launch) {
    if (!tabs.empty()) { return ...; }
    return {StartupTabs({StartupTab(chrome::ChromeUINewTabURLAsGURL())}), ...};
  }
  ```
  — hard-coded NTP returned **before** any pref check.
- `DetermineBrowserOpenBehavior()` at line 798-803 likewise blocks `SYNCHRONOUS_RESTORE` when `IS_POST_CRASH_LAUNCH` is set.
- `SessionRestore::RestoreSession()` is therefore **never called**.
- The existing `chromium_src/chrome/browser/ui/browser_tabrestore.cc` patch (which calls `MahoTabIdHelper::SetRestoredTabId` from `extra_data["maho_stable_tab_id"]`) is **dead code**.

**Why the profile is stuck "Crashed"**: `ExitTypeService::SetCurrentSessionExitType(kClean)` is invoked from `~ExitTypeService` (KeyedService teardown). For this destructor to run, `Profile` must be destroyed during a clean shutdown. If Maho is force-killed, OS-killed, or crashes before profile teardown, the exit type stays "Crashed" forever. Most likely cause in our development cycle: repeated `pkill -9 Maho.app` and crashes during iteration. **Even one normal Cmd+Q should fix it for next run** — but if that path also has issues, exit_type stays Crashed indefinitely.

### Layer 2 — `kRestoreOnStartup` default is `DEFAULT` (NEW_TAB) on macOS
- `chromium/src/chrome/browser/prefs/session_startup_pref.cc:62-78`:
  ```cpp
  registry->RegisterIntegerPref(prefs::kRestoreOnStartup,
                                TypeToPrefValue(GetDefaultStartupType()), flags);

  SessionStartupPref::Type GetDefaultStartupType() {
  #if BUILDFLAG(IS_CHROMEOS)
    return SessionStartupPref::LAST;
  #else
    if (features::kSetDefaultToContinueSession.Get()) {  // DISABLED_BY_DEFAULT
      return SessionStartupPref::LAST;
    }
    return SessionStartupPref::DEFAULT;  // ← falls through to NEW_TAB on macOS
  #endif
  }
  ```
- Maho has no override for this pref. Even when the user has never explicitly changed it (Preferences shows `session.restore_on_startup: None`), the default is `DEFAULT/NEW_TAB`.
- `ShouldRestoreLastSession()` returns false for `DEFAULT`, so `DetermineBrowserOpenBehavior()` never returns `SYNCHRONOUS_RESTORE` even without the crash guard.

### Layer 3 — Sidebar's render gate skips tabs without `WebContents`
- `maho_sidebar_state_adapter.cc::BuildTabListModel` reads from `maho_core_get_sidebar_tree` (Rust `tab_manager`). All 9 LMDB tabs are returned.
- `ParseTreeNode` cross-references each tab against `TabStripModel` via a `stable_id_map` keyed by `MahoTabIdHelper::stable_tab_id()`. Tabs with no matching `WebContents` keep `tab_strip_index = -1`.
- **The render gate at `maho_sidebar_tab_list_view.cc:2813-2817`**:
  ```cpp
  const int live_index = node.tab_strip_index >= 0
                             ? node.tab_strip_index
                             : ResolveTabStripIndexForTabId(node.tab_id);
  if (live_index < 0) {
    continue;  // ← row silently dropped
  }
  ```
- `MahoTabRegistry::Bootstrap()` is **unidirectional**: TabStripModel → Rust (announce existing WebContents to core). There is no reverse path that creates WebContents for LMDB-restored tabs.

### Execution chain (current state)
```
Launch
  → exit_type=Crashed → is_post_crash_launch=true
  → DetermineStartupTabs short-circuits → {chrome://newtab}     [Layer 1]
  → kRestoreOnStartup is DEFAULT/NEW_TAB anyway                 [Layer 2]
  → SessionRestore::RestoreSession() NEVER called
  → browser_tabrestore.cc UUID-preservation patch DEAD CODE
  → MahoTabRegistry::Bootstrap iterates TabStripModel (1 NTP)
  → Rust core load_state restores 9 tabs from LMDB
  → 8 of 9 LMDB tabs have no WebContents
  → tab_list_view.cc:2816 skips them                            [Layer 3]
  → Sidebar shows 1 NTP row only
```

---

## Solution: Approach A + Option X (Oracle-recommended)

**Approach A**: Use Chromium's `SessionService` as the source of truth for `WebContents` lifecycle. Make Maho's `tab_manager` (LMDB) the metadata layer. Bridge them via the already-existing `MahoTabIdHelper` UUID + `extra_data["maho_stable_tab_id"]` patches.

**Option X**: Tabs in `tab_manager` that have no corresponding `WebContents` are rendered as **suspended ghost rows** (dimmed, like Chrome's discarded tabs). Clicking a ghost row creates a `WebContents` with the persisted UUID pre-seeded, "waking" the tab.

---

## Implementation Plan (7 phases)

### Phase 1: Ensure clean exit_type write on shutdown

**File**: `maho-chromium/browser/maho_browser_main_extra_parts.cc`

**Change**: In `PostMainMessageLoopRun()` (line 150), explicitly mark the profile as cleanly exited before `Profile` teardown. Iterate active profiles and call `ExitTypeService::SetCurrentSessionExitType(ExitType::kClean)` for each, OR rely on `ProfileImpl::OnAppExiting` if it already does this.

**Investigation prerequisite**: Verify why upstream Chromium's clean-exit path isn't writing the value. Possible causes:
- `ExitTypeService` KeyedService isn't being destroyed because profile isn't being torn down
- `BrowserShutdown::OnShutdownStarting` isn't being invoked

**Concrete code**:
```cpp
void MahoBrowserMainExtraParts::PostMainMessageLoopRun() {
  // Force-write clean exit_type so next launch isn't treated as post-crash.
  ProfileManager* manager = g_browser_process->profile_manager();
  if (manager) {
    for (Profile* profile : manager->GetLoadedProfiles()) {
      if (profile->IsRegularProfile()) {
        if (auto* svc = ExitTypeService::GetInstanceForProfile(profile)) {
          svc->SetCurrentSessionExitType(ExitType::kClean);
        }
      }
    }
  }
  // ...existing maho-core save_state code...
}
```

**BUILD.gn**: add `//chrome/browser/sessions:sessions` dep to `maho_browser_main_extra_parts` source set.

### Phase 2: Override `kRestoreOnStartup` default to `LAST`

**File option A (preferred)**: New chromium_src override
- Add to `apply_chromium_src_overrides.py`: a REPLACEMENTS entry for `chrome/browser/prefs/session_startup_pref.cc::GetDefaultStartupType()` that returns `SessionStartupPref::LAST` unconditionally
- Or override at registration: replace `TypeToPrefValue(GetDefaultStartupType())` with `TypeToPrefValue(SessionStartupPref::LAST)` in line 62

**File option B (lighter, runtime override)**: `MahoBrowserMainExtraParts::PostProfileInit`
```cpp
void MahoBrowserMainExtraParts::PostProfileInit(Profile* profile, bool is_initial_profile) {
  if (!is_initial_profile) return;
  if (!profile || profile->IsOffTheRecord()) return;

  PrefService* prefs = profile->GetPrefs();
  // If user hasn't explicitly set the value, override the default to LAST
  // so SessionService restore kicks in.
  const PrefService::Preference* p = prefs->FindPreference(prefs::kRestoreOnStartup);
  if (p && p->IsDefaultValue()) {
    SessionStartupPref::SetStartupPref(profile,
        SessionStartupPref(SessionStartupPref::LAST));
  }
  // ...existing PostProfileInit code...
}
```

**Decision**: Option B (runtime override). Less invasive, no upstream patch. Honor user's explicit choice if they ever change Settings.

**BUILD.gn**: ensure `maho_browser_main_extra_parts` already has `//chrome/browser/prefs:util` or similar — the upstream `SessionStartupPref::SetStartupPref` exists in `session_startup_pref.cc`.

### Phase 3: (REMOVED — was redundant manual prefs verification)

### Phase 4: Sidebar ghost-row rendering

**File**: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc:2813-2817`

**Current**:
```cpp
if (live_index < 0) {
  continue;
}
```

**New**: Render the row but mark it suspended/dimmed. Set a flag on the row view (`SetSuspended(true)`) that styles it greyed out (alpha ~0.5) and suppresses live indicators (loading spinner, audio).

```cpp
const bool is_suspended = (live_index < 0);
// ... continue with row creation but skip live_index-dependent logic if suspended
auto* row_view = AddRowView(node);
row_view->SetSuspended(is_suspended);
```

Touch points:
- Add `bool is_suspended_` field to `SidebarTabRowView` (or equivalent) with `SetSuspended(bool)` setter
- In `SidebarTabRowView::OnPaint` or theme change: if suspended, paint with reduced alpha
- Skip favicon-loading and live-title updates for suspended rows (they already use persisted values)

### Phase 5: Click-to-wake handler

**File**: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` — wherever row-click invokes `TabStripModel::ActivateTabAt(live_index)`

**Behavior**: If `live_index < 0` (suspended ghost), instead of activate, create a new WebContents:

```cpp
void OnTabRowClicked(const std::string& tab_id) {
  int live_index = ResolveTabStripIndexForTabId(tab_id);
  if (live_index >= 0) {
    tab_strip_model_->ActivateTabAt(live_index);
    return;
  }

  // Suspended ghost — wake by creating a WebContents with persisted UUID
  WakeSuspendedTab(tab_id);
}

void WakeSuspendedTab(const std::string& tab_id) {
  // Look up persisted URL from Rust core
  auto persisted = QueryTabFromCore(tab_id);  // returns {url, title}
  if (!persisted) return;

  // Pre-seed UUID on the next-created WebContents so MahoTabIdHelper inherits it
  MahoTabIdHelper::SetPendingRestoredTabId(tab_id);

  NavigateParams params(browser_, GURL(persisted->url),
                        ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}
```

**Required helper** in `MahoTabIdHelper`: `static SetPendingRestoredTabId(const std::string& tab_id)` and consume it in `MahoTabIdHelper(WebContents*)` constructor (replace the freshly-generated UUID with the pending one if set).

### Phase 6: Post-restore reconciliation

**File**: `maho-chromium/browser/maho_browser_main_extra_parts.cc` or new `maho_session_reconciler.cc`

**Hook**: Subscribe to `SessionRestore::OnSessionRestoreFinishedLoadingTabs` (or equivalent). When fired:
1. Enumerate all `WebContents` across all browsers' TabStripModels — collect set of `MahoTabIdHelper::stable_tab_id`s.
2. Enumerate all tabs from Rust `tab_manager` for each space.
3. Diff: tabs in Rust but not in TabStripModel → already handled by Phase 4 ghost-rendering (no-op here).
4. Diff: tabs in TabStripModel but not in Rust → already handled by `MahoTabRegistry::Bootstrap` `create_tab` dispatch (no-op here).
5. Optional log: "Reconciliation: 9 LMDB tabs, 5 restored, 4 suspended."

This phase is largely **logging + invariant validation**. The actual reconciliation is implicit — Phase 4+5 handle the rendering, Bootstrap handles the data sync.

### Phase 7: Update `seed_initial.rs` to mark seeded tabs as Suspended

**File**: `maho/crates/maho-core/examples/seed_initial.rs`

**Change**: After dispatching `CreateTab` events, mutate the resulting tabs to `state: TabLifecycleState::Suspended { snapshot: TabSnapshot::default() }` (or expose a `SuspendTab` ShellEvent if one exists).

**Decision**: `TabLifecycleState::Suspended` requires a `TabSnapshot`. Two options:
- Option 7.A: Add a `SuspendTab { tab_id }` ShellEvent that sets state to Suspended with empty snapshot
- Option 7.B: Manipulate tab state directly in seed binary by setting up the Tab struct and calling `tab_manager.restore_tab(tab)` (needs internal API access — currently `tab_manager` is private in MahoCore)

**Decision**: 7.A (add SuspendTab event). Cleaner, reusable for future "freeze" feature.

After Phase 4 lands, seeded tabs auto-render as ghost rows regardless of their `state` field — because the gate is `live_index < 0`, not `state == Suspended`. So Phase 7 is **optional polish** for state-correctness; the core fix doesn't depend on it.

---

## Build & Test

After each Phase implementation:
```bash
python3 maho-chromium/build/scripts/build_maho.py    # full build
# OR for narrower iteration:
autoninja -C chromium/src/out/Default chrome
```

Test cycle:
```bash
pkill -9 -f "Maho\.app"; sleep 2
rm -rf "/Users/indo/Library/Application Support/Maho/MahoCore/state/"
rm -rf "/Users/indo/Library/Application Support/Maho/Default/Sessions/"
# Reset Preferences exit_type to Normal manually for first launch only
cd maho && cargo run --release --example seed_initial -- "/Users/indo/Library/Application Support/Maho/MahoCore/state/"
/Users/indo/.../Maho.app/Contents/MacOS/Maho > /tmp/maho.log 2>&1 &
# Inspect:
grep -E "Maho\] (load_state|save_state)|SessionRestore|SetRestoredTabId" /tmp/maho.log
```

---

## Acceptance Criteria

1. **Clean restart preserves tabs**: Open 5 tabs, Cmd+Q, relaunch. All 5 reappear with same UUIDs (verify via stderr log + LMDB inspection).
2. **Folders/favorites/pinned persist**: A tab pinned to a folder before quit retains its folder placement and pin status after restart.
3. **Seeded ghost rows render**: After `seed_initial`, sidebar shows 9 dimmed tabs in 6 folders + 3 favorites. The browser window is empty (no NTP).
4. **Click-to-wake works**: Clicking a dimmed ghost row creates a WebContents with the persisted URL and same UUID. Row un-dims. No duplicate Rust-side tab created.
5. **No double-create across cycles**: After 10 quit/restart cycles, `LMDB tab count == sidebar tab count == TabStripModel count + ghost count` for the active window. UUID set is stable.
6. **No NTP on startup when nothing to restore**: Empty LMDB + empty Sessions → blank window (Maho's existing zero-tab patch handles this; Phase 1 fixes Layer 1 NTP injection).
7. **Force-kill recovery**: After SIGKILL, next clean Cmd+Q fixes exit_type. Subsequent launches restore correctly.

---

## Risks & Mitigation

- **Phase 1 timing risk**: If `ExitTypeService` is destroyed before our explicit `SetCurrentSessionExitType` call runs, the override is no-op. Mitigation: verify call ordering with a debug LOG in shutdown sequence; if needed, hook into `BrowserShutdown::OnShutdownStarting` or `BrowserList::CloseAllBrowsers` post-callback.

- **Phase 2 timing risk**: `PostProfileInit` may run after the first `SessionService::Init` which reads the pref. If LAST isn't set yet, first launch still uses NEW_TAB. Mitigation: use chromium_src/ patch for the registration default (Option A) instead of runtime override.

- **Phase 5 wake race**: User clicks ghost while session restore is mid-flight → could double-create. Mitigation: gate `WakeSuspendedTab` on `Browser::is_session_restore_completed()` flag.

- **Per-window scoping**: SessionService restores per-Browser-window. If user had tabs in window A that's no longer in Sessions (e.g. closed-window history), those become suspended ghosts in the active window's space. This is acceptable given Maho's space-as-container model.

- **Existing user migration**: Users who have already accumulated "ghost tabs" in LMDB from pre-fix versions will see them as suspended ghosts on first launch after upgrade. Acceptable; click-to-wake handles them gracefully.

- **`MahoTabIdHelper::SetPendingRestoredTabId` race**: Two concurrent ghost-wake clicks could race the static pending-id storage. Mitigation: use a thread-local or sequence-checked field; set just before `Navigate()` and consume in next `MahoTabIdHelper` constructor (atomic).

- **GN file edits forbidden** (per AGENTS.md): chromium_src/ patches go through `apply_chromium_src_overrides.py` REPLACEMENTS table, not direct BUILD.gn edits.

---

## Implementation Order (4-step rollout)

1. **Step 1 (~1.5h)**: Phase 1 (exit_type) + Phase 2 (pref default). Build + test. Verify SessionService restore actually fires now via stderr log + sessions file inspection.
2. **Step 2 (~2h)**: Phase 4 (ghost row render) + Phase 5 (click-to-wake). Build + test.  Verify ghost tabs appear and wake on click.
3. **Step 3 (~1h)**: Phase 6 (reconciliation logging) + Phase 7 (seed binary update with SuspendTab event). Verify `seed_initial` produces only ghost rows on first launch.
4. **Step 4 (~30min)**: Acceptance criteria validation across all 7 items.

**Total estimated effort**: 5-6 hours including build cycles.

---

## Files Touched (summary)

| Phase | File | Change Type |
|-------|------|-------------|
| 1 | `maho-chromium/browser/maho_browser_main_extra_parts.cc` | Modify (PostMainMessageLoopRun) |
| 1 | `maho-chromium/browser/BUILD.gn` | Add dep `//chrome/browser/sessions:sessions` |
| 2 | `maho-chromium/browser/maho_browser_main_extra_parts.cc` | Modify (PostProfileInit) |
| 4 | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` | Modify (line 2813-2817) |
| 4 | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h` | Add suspended-row support |
| 5 | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` | Modify (click handler) |
| 5 | `maho-chromium/browser/maho_tab_id_helper.{h,cc}` | Add `SetPendingRestoredTabId` |
| 6 | `maho-chromium/browser/maho_browser_main_extra_parts.cc` (or new file) | Add session restore observer |
| 7 | `maho/crates/maho-types/src/events/shell_event.rs` | Add `SuspendTab` variant |
| 7 | `maho/crates/maho-core/src/event_dispatcher.rs` | Handle `SuspendTab` |
| 7 | `maho/crates/maho-core/examples/seed_initial.rs` | Dispatch `SuspendTab` per seeded tab |

---

## Existing Verified Mechanisms (DO NOT TOUCH)

- `MahoTabRegistry::OnTabStripModelChanged(kRemoved)` shutdown guard via `IsTryingToQuit()` — already in place, working
- `chromium_src/chrome/browser/ui/browser_live_tab_context.cc` patch — writes `extra_data["maho_stable_tab_id"]`
- `chromium_src/chrome/browser/ui/browser_tabrestore.cc` patch — calls `MahoTabIdHelper::SetRestoredTabId`
- LMDB save_state/load_state with telemetry log — already verified 9-tab round-trip works
- Maho zero-tab startup patch (`chromium_src/.../startup_browser_creator_impl.cc`) — preserves blank-window behavior when `prefs_tabs` is empty

These are the load-bearing components that make the fix possible. The fix is to **trigger the restore path that connects them**.
