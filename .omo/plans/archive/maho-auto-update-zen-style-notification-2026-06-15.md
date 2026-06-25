# Maho Sidebar — Zen-style Update Notification (replaces footer pill)

**Created**: 2026-06-15 JST
**Source**: Direct analysis of `zen-browser/desktop` at `src/zen/common/modules/ZenSidebarNotification.mjs` + `ZenUpdates.mjs` + `zen-sidebar-notification.css`. The current Maho footer "pill" implementation does not match Zen's structure, behavior, or UX and is to be discarded.
**Predecessor work** (already in working tree, will be partially removed):
- `MahoSidebarFooterUpdatePillModel` + `UpdatePillButton` in `MahoSidebarFooterView`
- `MahoSidebarUpdatePillController` (closure-based observer of `MahoUpdateManager`)
- `MahoSidebarStateAdapter::BuildUpdatePillModel` (state→model mapping)
- `--maho-simulate-update-state` debug switch (kept; the simulator still drives the new view)
**Status**: Plan-only.

---

## 0. Goal

Reproduce Zen's update notification UX, expressed in Chromium Views, anchored to the sidebar between the tab list and the footer separator.

### Reference behavior (extracted from Zen source)

| Zen aspect | Concrete spec |
|---|---|
| Component | `<zen-sidebar-notification>` Lit web component |
| DOM placement | Inserted as next sibling of `#zen-media-controls-toolbar` (i.e. inside the sidebar, near the bottom) |
| Card structure | header (heading text + close button) + 1px divider + body (list of action rows) |
| Style | `:host { background: surface tinted with primary 5%, border-radius: medium, box-shadow: 0 0 6px rgba(0,0,0,0.1) }` |
| Header | centered title (truncates with ellipsis), close `[×]` button top-right with hover bg |
| Body row | clickable area with icon + label, opacity 0.7 default → 1.0 on hover |
| "special" row | animated text gradient (saturate 3x, 4-color linear-gradient sweeping over 2s) |
| Trigger | startup version compare against `zen.updates.last-version` pref; one-shot per upgrade |
| Dismiss | close button removes card; pref already updated → won't re-appear until next upgrade |
| Animation in | `opacity: 0→1, y: 50→0`, delay 1s (slide up from below + fade) |
| Animation out | `opacity: 1→0, y: 0→10` (slight slide down + fade) |
| Multi-action | `links: [{ l10nId, icon, special, action | url }]` array |
| Collapsed sidebar | hidden via `:root:not([zen-sidebar-expanded="true"]) zen-sidebar-notification { display: none; }` |

### What this plan ships
- `MahoSidebarUpdateNotificationView` Views class — header/body/close, shadow, dismiss.
- Version-change trigger driven by a new `maho.update.last_seen_version` local-state pref.
- Slide-up + fade-in entry, slide-down + fade-out exit using `views::AnimationBuilder`.
- Multi-action API (e.g. ["Release notes", "Restart in safe mode"]) — initial Maho usage exposes "Restart now" + "What's new".
- Hidden when sidebar is collapsed (Maho equivalent: `MahoSidebarView` is in collapsed mode).
- The `--maho-simulate-update-state` switch continues to work; in simulate mode the notification appears regardless of `last_seen_version` so visual QA is still 1-command.

### What this plan removes
- `MahoSidebarFooterUpdatePillModel` (model)
- `UpdatePillButton` (the static-row button)
- The `update_pill_*` member group on `MahoSidebarFooterView`
- `MahoSidebarFooterView::UpdateUpdatePill()` and `OnUpdatePillPressed()`
- `MahoSidebarStateAdapter::BuildUpdatePillModel()` (the pill state→model mapping)
- The `MahoSidebarFooterPillModelTest` suite

### Non-goals
- Multi-window animation polish (each `MahoSidebarView` shows its own notification independently — fine).
- Rich CTA gradient ("special" row animation) — explicit follow-up. v1 ships static action rows.
- Localization — strings stay in English with `// TODO(maho-i18n)` flagged.
- Replacing `views::Button` for actions with custom HoverableLabel — `views::Button` is sufficient.

### Success criteria (binary)
- [ ] `bun run typecheck` / `bun run test` clean.
- [ ] `python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target unit_tests` succeeds.
- [ ] `--ninja-target chrome` succeeds.
- [ ] All existing `MahoUpdate*Test` and `Signature*Test` cases pass (no regression in update subsystem).
- [ ] Existing `MahoSidebar*Test` cases pass (no regression in sidebar).
- [ ] New `MahoSidebarUpdateNotificationViewTest` (≥6 tests) passes.
- [ ] Manual smoke on macOS: launching with `--maho-simulate-update-state=ready_to_install` shows the notification card with shadow, animated entry, and a working close button. Subsequent launches with the same simulate flag still show it (simulate bypasses `last_seen_version` gate).
- [ ] Manual smoke without simulate flag, two-version test: bump version, restart, notification appears once and is dismissible. Restarting after dismissal does not show it again until next version bump.

---

## 1. Architecture

```
                                   ┌────────────────────────────────────┐
                                   │   MahoUpdateManager (singleton)    │
                                   │   state, last_error, channel ...   │
                                   └─────────────────┬──────────────────┘
                                                     │ MahoUpdateObserver
                                                     ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  MahoSidebarUpdateNotificationController         (owned by MahoSidebarView) │
│   - decides "should the notification be visible"                            │
│   - reads MahoUpdateManager state + UpdateError                             │
│   - reads/writes maho.update.last_seen_version (one-shot per version)       │
│   - simulate-mode override                                                  │
│   - exposes MahoSidebarUpdateNotificationModel + dismiss/action callbacks   │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │ Update(model)  /  on dismiss (close X)
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  MahoSidebarUpdateNotificationView : views::View                            │
│   ┌─ header_ ───────────────────────────────────────────┐                   │
│   │ heading_ Label  ............................ close_ ImageButton│       │
│   ├─────────────────────────────────────────────────────┤                   │
│   │ body_:                                                                  │
│   │   row(icon, "Restart now",     special=true,  action=Apply)             │
│   │   row(icon, "What's new",      special=false, action=OpenReleaseNotes)  │
│   └─────────────────────────────────────────────────────┘                   │
│  shadow via gfx::ShadowValues + SetPaintToLayer                             │
│  animate in/out via views::AnimationBuilder (opacity + transform)           │
└─────────────────────────────────────────────────────────────────────────────┘
```

### View tree (host = `MahoSidebarUpdateNotificationView`)

```
MahoSidebarUpdateNotificationView (BoxLayout vertical, padding 0, gap 0)
├── header_  (BoxLayout horizontal, padding 16)
│   ├── heading_           (views::Label, centered, ellipsized)
│   └── close_button_      (views::ImageButton, top-right, 18x18, rounded 4px)
├── divider_  (1px line, color = currentColor 10%)
└── body_  (BoxLayout vertical, padding 6, spacing 2)
    ├── action_row[0]      (views::Button, hover 0.7→1.0 opacity)
    │   ├── icon ImageView
    │   └── label
    ├── action_row[1]
    │   └── ...
    └── ...
```

### Data model

```cpp
// new: maho_sidebar_update_notification_model.h
namespace maho {

struct MahoSidebarUpdateActionModel {
  std::u16string label;
  std::u16string accessible_label;
  ui::ImageModel icon;
  bool special = false;
  base::RepeatingClosure on_activate;
};

struct MahoSidebarUpdateNotificationModel {
  bool visible = false;
  std::u16string heading;
  std::vector<MahoSidebarUpdateActionModel> actions;
  base::RepeatingClosure on_dismiss;
};

}  // namespace maho
```

This replaces `MahoSidebarFooterUpdatePillModel`. Note: the `Action` enum on the old model goes away — actions are now fully encapsulated as closures inside the model, so the view never needs to know what each row "means".

### Visibility decision (Controller)

Pseudo-code:

```
should_show =
    !is_disabled
    && !is_crash_loop
    && (
         simulate_mode_active           // sim flag bypasses everything below
       || (
            state == kReadyToInstall
            && current_version != last_seen_version_pref
            && !user_dismissed_this_version_in_session
          )
       )
```

Note this is **stricter** than the previous pill controller, which surfaced visible UI for `kChecking`, `kDownloading`, `kVerifying`, and `kError`. Zen only shows the notification on actual *upgrade*. We follow Zen for v1; intermediate states (downloading etc.) become silent again. Manual checks ("Settings → Check for updates") would be served by Phase F4 settings UI, not the notification.

### Pref additions

```
maho.update.last_seen_version  (string, local-state, default empty)
```

Set when:
- User dismisses the notification → set to current version
- Notification has been displayed for ≥ N seconds without dismissal → set after first auto-fade (TBD, probably skip for v1 — only set on dismiss)

The `Initialize()` flow already handles the rest (kill-switch, install_id, crash sentinel). The new controller hooks into the existing observer chain.

---

## 2. Phasing

Single PR; tasks marked `‖` are parallelizable.

### Phase Z1 — Model + Controller (replaces existing pill controller)

- **Z1.1** Delete `MahoSidebarFooterUpdatePillModel` from `maho_sidebar_state_models.h`/`.cc`. Remove `update_pill` field from `MahoSidebarFooterModel`.
- **Z1.2** Delete `BuildUpdatePillModel` from `MahoSidebarStateAdapter`. Remove the call in `BuildFooterModel`.
- **Z1.3** Add new files:
  - `maho_sidebar_update_notification_model.h` (struct above)
  - `maho_sidebar_update_notification_controller.{cc,h}` (owns state, builds model, exposes `BuildModel(BrowserContext*)` + `Dismiss()`)
- **Z1.4** Move version-comparison logic into the controller. Read/write `kMahoUpdateLastSeenVersion` local-state pref.
- **Z1.5** Drop `MahoSidebarUpdatePillController` (the closure-only observer) and replace usage with the new controller. Sidebar still owns the new controller and binds to `ScheduleRefreshAll()`.
- **Z1.6** Pref registration: add `kMahoUpdateLastSeenVersion` to `maho_update_pref_names.h` + `RegisterLocalStatePrefs` in `maho_update_prefs.cc`.

### Phase Z2 — View (the actual card)

- **Z2.1** New file `maho_sidebar_update_notification_view.{cc,h}`:
  - Class `MahoSidebarUpdateNotificationView : public views::View`.
  - Builds header (heading Label + close ImageButton) and body (vector of action Buttons).
  - `Update(const MahoSidebarUpdateNotificationModel&)` rebuilds children to match the model (cheap; only fires on state transitions).
  - `OnPaint` — applies `gfx::ShadowValues` to the layer (or paints shadow via `cc::PaintFlags::SetLooper`).
- **Z2.2** Style: rounded corners (`kBorderRadiusMedium = 12px`), background = `kColorSysSurface1` mixed with primary at 5%, shadow `0 0 6px rgba(0,0,0,0.1)`.
- **Z2.3** Close button: 18×18 ImageButton, lucide-x icon, hover bg = `kColorSysOnSurface` at 15% alpha. On press → invoke `model.on_dismiss`.
- **Z2.4** Action row: `views::Button` with horizontal `BoxLayout` (icon + label), label gets `STYLE_BODY_5` typography, opacity 0.7 default → 1.0 hover. On press → `action.on_activate`.
- **Z2.5** "special" row: for v1, render with `kColorSysPrimary` text color (no animation gradient — explicit follow-up).

### Phase Z3 — Animations

- **Z3.1** `views::AnimationBuilder` chain on visibility change:
  - Show: opacity 0→1 over 200ms, transform.translate.y 12→0 over 200ms ease-out
  - Hide: opacity 1→0 over 150ms, transform.translate.y 0→6 over 150ms ease-in
- **Z3.2** Animation runs on the View's layer (already paint-to-layer for shadow). After hide animation, `SetVisible(false)`.

### Phase Z4 — Sidebar wiring

- **Z4.1** `MahoSidebarFooterView`: remove all `update_pill_*` members and the entire `UpdateUpdatePill()` / `OnUpdatePillPressed()` methods + the `UpdatePillButton` class. Footer regains its old shape (separator → space dots → footer buttons).
- **Z4.2** `MahoSidebarView`:
  - Owns `std::unique_ptr<MahoSidebarUpdateNotificationController>`.
  - Owns `raw_ptr<MahoSidebarUpdateNotificationView>` placed between tab scroll view and footer view (in the box layout). The View's intrinsic size collapses to 0 when `model.visible == false`, so when no notification it does not occupy layout space.
  - `RefreshAll()` calls `notification_view_->Update(controller_->BuildModel(...))`.
- **Z4.3** When the sidebar is collapsed, `MahoSidebarView::ApplyViewState()` skips updating the notification (or sets `model.visible = false`). Matches Zen's `:root:not([zen-sidebar-expanded="true"])` rule.

### Phase Z5 — Action bindings

For v1, the notification surfaces two actions on `kReadyToInstall`:

- **Restart now** (special=true) — invokes `MahoUpdateManager::ApplyUpdateAndRestart()`.
- **What's new** — opens `https://maho.dev/release-notes/{version}` in a new tab via `chrome::AddTabAt(browser_, ..., true)`.

Controller is responsible for binding closures and handing the model to the view.

### Phase Z6 — Tests

- **Z6.1** New `maho_sidebar_update_notification_controller_unittest.cc`:
  - `ShowsOnReadyToInstallForFreshVersion`
  - `HiddenWhenLastSeenVersionEqualsCurrent`
  - `DismissPersistsLastSeenVersion`
  - `SimulateModeBypassesVersionCheck`
  - `KillSwitchSuppressesNotification`
  - `CrashLoopSuppressesNotification`
- **Z6.2** New `maho_sidebar_update_notification_view_unittest.cc`:
  - `RendersHeadingAndCloseButton`
  - `RendersAllActionRows`
  - `ClickingCloseInvokesOnDismiss`
  - `ClickingActionRowInvokesOnActivate`
  - `EmptyModelHidesView` (visible=false → preferred size = 0)
  - `RebuildsChildrenOnUpdate` (changing actions array → child count matches new array)
- **Z6.3** Delete `maho_sidebar_state_adapter_unittest.cc` (its tests no longer apply since the pill model is gone).

### Phase Z7 — Visual smoke

`--maho-simulate-update-state=ready_to_install` launches with the notification visible. Manual screenshot check.

---

## 3. File-by-File Summary

```
# New
maho-chromium/browser/ui/views/sidebar/maho_sidebar_update_notification_model.h
maho-chromium/browser/ui/views/sidebar/maho_sidebar_update_notification_controller.{cc,h}
maho-chromium/browser/ui/views/sidebar/maho_sidebar_update_notification_view.{cc,h}
maho-chromium/browser/ui/views/sidebar/maho_sidebar_update_notification_controller_unittest.cc
maho-chromium/browser/ui/views/sidebar/maho_sidebar_update_notification_view_unittest.cc

# Modified
maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.{cc,h}     (own controller + view)
maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.{cc,h}  (remove all pill code)
maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.{cc,h}  (remove BuildUpdatePillModel)
maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_models.{cc,h}   (remove pill model)
maho-chromium/browser/ui/views/sidebar/BUILD.gn                            (sources + test sources)
maho-chromium/browser/updates/maho_update_pref_names.h                    (kMahoUpdateLastSeenVersion)
maho-chromium/browser/updates/maho_update_prefs.cc                        (register new pref)

# Deleted
maho-chromium/browser/ui/views/sidebar/maho_sidebar_update_pill_controller.{cc,h}
maho-chromium/browser/ui/views/sidebar/maho_sidebar_update_pill_controller_unittest.cc
maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter_unittest.cc
```

**Total**: 1 PR, ~12 files (5 new, 7 modified, 3 deleted), no new external deps.

---

## 4. Verification Plan

```sh
# Type / Rust workspace sanity
bun run typecheck && bun run test

# Build narrow target
python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target unit_tests

# Run new + existing maho update + sidebar tests
chromium/src/out/Default/unit_tests \
    --gtest_filter='MahoUpdate*:Signature*:MahoSidebarUpdateNotification*:MahoSidebar*' \
    --test-launcher-jobs=1

# Full chrome
python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target chrome

# Visual smoke
PROFILE=/tmp/maho-zen-notif-demo
rm -rf $PROFILE && mkdir -p "$PROFILE/Default"
echo '{"maho":{"welcome":{"completed":true},"update":{"enabled":true}}}' > "$PROFILE/Default/Preferences"
echo '{"maho":{"welcome":{"completed":true},"update":{"enabled":true}}}' > "$PROFILE/Local State"
chromium/src/out/Default/Maho.app/Contents/MacOS/Maho \
    --user-data-dir=$PROFILE \
    --no-first-run \
    --enable-logging=stderr --v=0 \
    --maho-simulate-update-state=ready_to_install
# Verify: card with shadow appears between tab list and footer separator,
# slides up + fades in, has heading + close + 2 action rows.
# Click close: card animates out + disappears.
```

---

## 5. Risks & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Animation timing creates layout flicker (notif appears, animates, layout reflows) | Med | Med | Notification view is paint-to-layer; transforms are GPU-composited; layout is committed once SetVisible flips |
| Shadow rendering + Maho's existing layer-effects conflict | Low | Med | Use `gfx::ShadowValues` on the layer; layer set as `kColorSysSurface1` mixed bg; verify on Mac (existing patterns: MahoSidebarFolderHoverPopup uses similar) |
| Removing `MahoSidebarFooterUpdatePillModel` breaks sidebar tests that reference it | Med | Low | One existing test (`maho_sidebar_edge_cases_interactive_test.cc:676`) reads `state.footer.update_pill.visible` — needs adjustment or deletion |
| User dismisses notification but state loops back to `kReadyToInstall` because relay returns same manifest | High (in dev) | Low | `last_seen_version` pref blocks re-show even when state changes back; dev can clear pref to re-test |
| Sidebar-collapsed detection mismatch (no equivalent attribute in Maho) | Med | Low | Use `MahoSidebarView::IsSidebarVisible()` (exists per existing API) and skip Update() when collapsed; v1 may also just always-render and let the view's preferred size collapse to 0 |
| Multi-window: each MahoSidebarView shows its own notification, all observing same manager | Low | Low | Acceptable: each window dismisses independently. If user dismisses in window A but window B still shows, both are reading the same `last_seen_version` pref so window B will hide on next state transition. Acceptable v1 behavior. |
| Existing `--maho-simulate-update-state` tests assert on pill-model fields that no longer exist | High | Low | Update those tests to check the new notification model instead |

---

## 6. Open Questions (resolve before Z1 starts)

1. **"Special" row gradient animation**: ship now or defer? Recommend defer — gradient text in Views requires a custom Label subclass with timer-driven repaints. Marking as `// TODO(maho-visual-polish)` after v1.
2. **Show on intermediate states (kChecking/kDownloading)?** Plan says no (Zen-style). Keep `--maho-simulate-update-state=checking` viable for visual debug only — controller respects simulate-mode bypass.
3. **Auto-dismiss timeout?** Zen does not auto-dismiss. Keep manual dismiss only.
4. **Place notification above or below tab list?** Zen places after `#zen-media-controls-toolbar` which is bottom-of-sidebar. We place above the footer separator (between tab list and space dots) — visually equivalent.

---

## 7. Self-Rollout

The kill-switch + crash-loop guards still apply. The notification cannot appear unless the user has already opted into auto-update. Simulate flag works in any build but only forces the notification visible if not crash-loop / not disabled.

No production behavior change beyond what Phase F1+F2+F3 already shipped. This is a UI replacement, not a logic change.

---

## 8. Out-of-Scope Follow-ups

| Item | Reason for deferral |
|---|---|
| "Special" gradient text animation | Custom paint required; v1 uses solid primary color |
| Toolbar/menu badge for `kReadyToInstall` | Plan F4.3 already deferred this |
| `chrome://maho-settings#updates` "Check now" / channel selector | Plan F4.1/F4.2 already deferred |
| Localized resource strings | i18n workstream |
| Notification across multiple `MahoSidebarView` instances coordinated dismiss | YAGNI for v1; dismissal is per-window |
| Telemetry on notification clicks | Analytics workstream |
