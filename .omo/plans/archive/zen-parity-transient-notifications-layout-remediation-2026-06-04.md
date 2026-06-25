# Zen-Parity Transient Notifications — Layout & Style Remediation Plan

**Date**: 2026-06-04
**Author**: Sisyphus
**Status**: Ready for execution
**Supersedes**: The Phase 1 layout/style contract in `zen-parity-transient-notifications-plan-2026-06-04.md` (specifically §4.1 goal 4, §5 Phase 1 "Behavior", and the anchor math/styling that landed in `maho_notification_overlay.cc`).
**Does not supersede**: The phase scoping (§2 categories), the callsite list (§10), the non-goals (§6), or the verification framework (§7) of the prior plan. Those remain authoritative.

---

## §1 Problem Statement

Phase 1 of the Zen-parity transient notifications work shipped a functional `MahoNotificationOverlay` primitive. The Rust/C++ wiring, lifetime model (per-`BrowserView` map), animation scaffold, and replace-on-show semantics are sound. **The visual layout is not Zen-parity.** It anchors top-center of the content area and uses Maho's command-bar chrome rather than Zen's dedicated toast container styling.

The mismatch traces back to an incorrect assumption baked into the original plan, not to a flawed implementation of that plan. The plan said "top-center"; the implementation correctly delivered top-center. The plan was wrong. We need to:

1. Correct the layout/style contract.
2. Reposition the existing primitive.
3. Restyle the existing notification view.

**This plan does not claim visual parity has been achieved.** It documents the gap and the path to close it. No code in `maho_notification_overlay.cc` is changed by this plan; that is the next implementation pass's job.

---

## §2 Verified Zen Behavior

Audit target: Zen Browser's `gZenUIManager.showToast(...)` surface and the host container in the browser chrome.

### 2.1 Default placement

- Toasts render inside a dedicated container (Zen DOM id pattern: `#zen-notification-container`) anchored to the **top-right** corner of the browser content area.
- Vertical offset is a small fixed gap from the top of the content region (visually ~12–20 px below the top edge of content; not flush against the toolbar).
- Horizontal offset is a small fixed gap from the **right edge** of the content region (mirrors the left-edge gap in mirrored layouts; visually ~12–20 px).
- The container is content-area scoped, not viewport scoped — it does not float over the sidebar or window chrome.

### 2.2 Layout-mirror behavior

- Zen has a user-configurable layout: sidebar can sit on the **left** (default) or **right**.
- When the sidebar is **left** (default), toasts anchor **top-right** of the content area.
- When the sidebar is **right** (mirrored chrome), toasts flip to **top-left** of the content area, mirroring the chrome.
- The flip is layout-driven, not animation-driven. It is determined at the moment of show; live re-flip on layout change is not required (Zen does not do this either).
- There is no "follow the user's reading direction" RTL behavior beyond this layout flip. The flip is tied to sidebar side, not text direction.

### 2.3 Visual chrome

- Toast surface uses Zen's notification chrome: rounded rectangle (~10–12 px radius), subtle drop shadow, surface color from Zen's notification palette (a dedicated background, not the command bar / urlbar palette).
- Border is hairline, lower contrast than Maho's current `kMahoColorCommandBarPanelBorder`.
- Title is the dominant element; body is a smaller, dimmer second line and is hidden when empty.
- Padding is roughly 10 vertical / 16 horizontal (Maho's current values are coincidentally close, so padding likely does not need to change).

### 2.4 Animation

- On show: slide-in from **above** the anchor with fade. Translate distance is small (~12–20 px). Duration is ~180 ms with an ease-out curve.
- On dismiss: slide-out **upward** with fade. Duration ~150–180 ms.
- Replace-on-show snaps the existing widget to its resting state and resets the dismiss timer; it does not re-animate from above. Maho's current behavior matches this.
- Auto-dismiss timeout is ~3000 ms. Maho's current value matches.

### 2.5 What Zen does NOT do (kept for clarity)

- No bottom-anchored toasts.
- No center-anchored toasts (top-center, bottom-center, or otherwise).
- No follow-the-cursor or anchor-to-source-element placement.
- No stacking of multiple visible toasts (latest replaces earlier; matches Maho).
- No actions inside toasts (no Undo button). Maho already matches.

---

## §3 Current Maho Mismatch

The current `maho-chromium/browser/ui/notifications/maho_notification_overlay.cc` (post Phase 1) renders correctly but differs from Zen on three axes: anchor, color tokens, and (minor) animation origin.

### 3.1 Anchor math (incorrect)

Both code paths (initial create and replace-on-show) compute:

```cpp
gfx::Rect content_bounds =
    browser_view_->contents_web_view()->GetBoundsInScreen();
int x = content_bounds.x() + (content_bounds.width() - view_size.width()) / 2;
int y = content_bounds.y() + 16;
```

This yields **top-center** of the content area at a 16 px top inset. Zen's correct math is right-anchored (or left-anchored when mirrored):

```cpp
// Right-anchored (default layout):
int x = content_bounds.right() - view_size.width() - kEdgeInset;
// Left-anchored (mirrored layout):
int x = content_bounds.x() + kEdgeInset;
```

`kEdgeInset` should be a single shared constant (16 px is fine; matches the existing 16 px top inset and Zen's visual feel).

### 3.2 Color tokens (incorrect)

`MahoNotificationView::OnThemeChanged` currently uses:

- Background: `kMahoColorCommandBarBackground`
- Border: `kMahoColorCommandBarPanelBorder`
- Title text: `kMahoColorPrimaryText`
- Body text: `kMahoColorSecondaryText`

The command-bar tokens are designed for the (much larger, more opaque) command palette surface. Zen's notification surface is a smaller, lighter chrome. Maho's color system needs dedicated notification tokens (or a deliberate decision to alias to existing tokens that visually match Zen — but **not** the command-bar pair).

### 3.3 Animation origin (minor)

Animations currently translate by `-20` on the Y axis (slide from above). The vertical axis is correct for Zen. There is no horizontal slide. This is acceptable; Zen also slides vertically only. **No remediation required for animation timing or axis.** Listed here only to confirm it is reviewed and intentional.

### 3.4 Layout-mirror awareness (missing)

The current implementation has no concept of sidebar-side. There is a single anchor formula. Zen flips horizontal anchoring with sidebar side. The remediation must read whichever Maho-side state already represents "sidebar on right" (likely a `MahoBrowserController` / `MahoSidebarContainerView` flag — to be confirmed during execution; do not invent a new pref).

---

## §4 Goals and Non-Goals

### 4.1 Goals

1. Reposition toasts to **top-right** of the content area in default layout, with a fixed edge inset matching the existing top inset (16 px).
2. Mirror to **top-left** when the sidebar/chrome is on the right side. Read the existing layout-side state; do not introduce a new pref or mode flag.
3. Replace the command-bar color tokens with notification-specific tokens that visually align with Zen's notification surface.
4. Keep the animation as a vertical slide-from-above + fade. Confirm timing constants stay at 180 ms in / 160 ms out / 3000 ms dismiss.
5. Preserve all existing functional behavior: per-`BrowserView` isolation, replace-on-show, sequence checks, widget-destruction safety.

### 4.2 Non-Goals

- Do **not** re-architect the primitive. No new public API, no new singletons, no per-toast configurability.
- Do **not** add stacking, queueing, or multi-toast support. Replace semantics stay.
- Do **not** add toast actions (Undo, dismiss button, etc.).
- Do **not** add per-callsite styling overrides. Title + optional body remain the entire surface.
- Do **not** broaden into adjacent notification surfaces: sidebar notification dot, infobar, onboarding overlays, instructional hints, recovery dialogs — all explicitly out of scope per the parent plan §6.
- Do **not** add live re-flip on sidebar-side change while a toast is visible. Anchor is decided at show-time. Zen does not live-re-flip and the added complexity is not worth it.
- Do **not** adjust phases 2–4 callsite scope. Callsite list in the parent plan §10 stands.
- Do **not** modify GN files. Sources are already in `browser/BUILD.gn`; remediation is pure C++ edit.

---

## §5 Remediation Phases

### Phase R1 — Color tokens

**File**: `maho-chromium/browser/ui/theme/maho_color_id.h` (and the corresponding mixer)

Add (or alias, if existing tokens are visually correct after audit) dedicated tokens:

- `kMahoColorNotificationBackground`
- `kMahoColorNotificationBorder`
- `kMahoColorNotificationPrimaryText`
- `kMahoColorNotificationSecondaryText`

If the audit shows that `kMahoColorPrimaryText` / `kMahoColorSecondaryText` are already the right values for notification text, alias the new tokens to them rather than duplicating values. The point of distinct token IDs is to avoid coupling the notification surface to command-bar drift over time.

Update `maho_color_mixer.cc` with the new entries. Pick values that visually match Zen's notification chrome: a slightly lighter / less opaque background than the command bar, hairline border, full-contrast title, dimmed body.

**R1 acceptance**:
- [ ] New token IDs declared and provided by the mixer for both light and dark.
- [ ] `python3 maho-chromium/build/scripts/build_maho.py` exits 0.
- [ ] No callsite outside `maho_notification_overlay.cc` references the new tokens (scope hygiene).

### Phase R2 — Anchor and mirror

**File**: `maho-chromium/browser/ui/notifications/maho_notification_overlay.cc`

Introduce a private helper:

```cpp
// Returns the on-screen rect for the toast given the current content bounds,
// the toast's preferred size, and the active sidebar side.
gfx::Rect ComputeToastBounds(const gfx::Rect& content_bounds,
                             const gfx::Size& view_size,
                             bool sidebar_on_right);
```

Behavior:

- `kEdgeInset` constant (16 px) used for both top and side gaps.
- `sidebar_on_right == false` (default): anchor to `content_bounds.right() - view_size.width() - kEdgeInset`, top inset `+kEdgeInset`.
- `sidebar_on_right == true`: anchor to `content_bounds.x() + kEdgeInset`, top inset `+kEdgeInset`.
- Width is the toast's preferred width (no clamping for now; Zen toasts are short single-line strings).

Replace both call paths inside `Show(...)` (the create branch and the replace branch) with calls to `ComputeToastBounds`. The sidebar-side bool comes from the existing layout state — to be confirmed during execution. If no such state is reachable from `BrowserView`, raise it as a question before adding a new accessor.

**R2 acceptance**:
- [ ] Toast renders top-right of the content area in default layout.
- [ ] Toast renders top-left when the sidebar is configured on the right.
- [ ] Replace-on-show preserves the correct anchor (does not snap to center or wrong side).
- [ ] No regression in per-`BrowserView` isolation: opening two windows side-by-side, triggering a toast in each, shows each toast in the correct window.

### Phase R3 — View styling

**File**: `maho-chromium/browser/ui/notifications/maho_notification_overlay.cc` (the namespace-local `MahoNotificationView`)

In `OnThemeChanged`, replace the four color references:

- `kMahoColorCommandBarBackground` → `kMahoColorNotificationBackground`
- `kMahoColorCommandBarPanelBorder` → `kMahoColorNotificationBorder`
- `kMahoColorPrimaryText` → `kMahoColorNotificationPrimaryText`
- `kMahoColorSecondaryText` → `kMahoColorNotificationSecondaryText`

Padding (`gfx::Insets::VH(10, 16)`), corner radius (`12`), and border thickness (`1`) stay as-is unless the visual review in R4 calls them out.

**R3 acceptance**:
- [ ] `lsp_diagnostics` clean on `maho_notification_overlay.cc`.
- [ ] No remaining reference to `kMahoColorCommandBar*` from within the notification view.
- [ ] Build passes.

### Phase R4 — Visual review

Manual smoke against a reference Zen build (or a current Zen screenshot bank if a working build is unavailable):

- Default layout: toast appears top-right; spacing from top and right edge looks balanced; background and border read as a notification, not a mini command bar.
- Mirrored layout: toast appears top-left; spacing matches mirrored.
- Title-only and title+body cases both render correctly (no body label collapse glitch).
- Animation: slide from above is unchanged and feels smooth; replace-on-show does not re-animate.
- Dark / light theme both look correct.

**R4 acceptance**:
- [ ] Side-by-side comparison with Zen reference shows no obvious chrome mismatch.
- [ ] If padding / radius / border / shadow needs adjustment, scope it tightly inside R4 and document the chosen values; do not expand into a generic theming refactor.

---

## §6 Affected Files

**Touched by this remediation**:
- `maho-chromium/browser/ui/notifications/maho_notification_overlay.cc` — anchor math, color tokens.
- `maho-chromium/browser/ui/theme/maho_color_id.h` — new notification color IDs.
- `maho-chromium/browser/ui/theme/maho_color_mixer.cc` — provide values for the new IDs.

**Not touched**:
- `maho_notification_overlay.h` — no public API change.
- `browser/BUILD.gn` — sources already listed.
- Any callsite from Phases 2–4 of the parent plan — those keep using the same `Show(title, body)` API.

If the audit in Phase R2 finds that sidebar-side state is not exposed via `BrowserView`, surface that gap and decide with the user before adding a new accessor. Do not silently bolt on a new path.

---

## §7 Verification Plan

### 7.1 Build and static checks

- `python3 maho-chromium/build/scripts/build_maho.py` exits 0 after R1, R2, R3.
- `lsp_diagnostics` clean on every edited file after each phase.
- No new GN edits; if any are required, stop and request explicit user approval per `maho-chromium/AGENTS.md`.

### 7.2 Manual smoke

Run the built `out/Default/Chromium.app/Contents/MacOS/Chromium`:

- Trigger a toast (use any wired Phase 2 callsite, e.g. Cmd-L copy URL).
- Verify top-right placement in default layout.
- Switch sidebar to right side; trigger again; verify top-left placement.
- Open two windows; trigger toast in each; verify each appears in its own window.
- Trigger two toasts in quick succession; verify the second replaces the first without re-animation and dismiss timer resets.
- Verify dismiss after ~3 s with slide-up + fade.
- Verify dark and light themes.

### 7.3 Regression guards

- The four parent-plan acceptance items for Phase 1 still hold:
  - Replace-on-show works.
  - No widget leak on rapid-fire calls.
  - Destroying the `BrowserView` while a toast is live does not crash.
  - No `LOG(INFO)` slop reintroduced.
- Phases 2–4 of the parent plan, if landed by the time this remediation runs, must continue to fire toasts on the same callsites with the same strings.

---

## §8 Risks and Mitigations

| Risk | Mitigation |
|---|---|
| Sidebar-side state not directly reachable from `BrowserView` | Stop and ask before adding a new accessor; avoid drilling through layers |
| New color tokens drift over time and become wrong again | Tokens are notification-specific; reviewers will flag any callsite outside the notification view |
| Visual review subjective; bikeshedding on padding / radius | R4 adjustments stay scoped; if more than one round of tweaks is needed, escalate to the user |
| Mirrored-layout users are a minority; flip path under-tested | Manual smoke explicitly covers both layouts; future Appium coverage can pin behavior |
| Replace-on-show after layout flip during the toast's lifetime | Anchor is decided at show-time only; do not implement live re-flip |
| Breaking the Phase 1 functional contract while restyling | R2 and R3 keep the public API and lifetime model untouched; only constants and one helper change |

---

## §9 Non-Goals (Explicit)

To keep this remediation tight and prevent scope creep:

- No changes to which user actions emit toasts.
- No changes to localized strings, plural handling, or copy.
- No additions to the public `MahoNotificationOverlay` API.
- No new Mojo, no new WebUI, no new prefs.
- No changes to sidebar notification dot, infobar, onboarding overlay, or any other notification surface listed in the parent plan §6.
- No revisit of Phases 2–4 callsite scope from the parent plan.
- No animation engine swap; only constants and direction stay under review.

---

## §10 Done Definition

This remediation is done when:

- Toasts render top-right of content (top-left in mirrored layout) in `out/Default` builds.
- Notification view uses notification-specific color tokens, not command-bar tokens.
- Manual smoke from §7.2 passes for default and mirrored layouts, light and dark themes.
- `build_maho.py` exits 0; `lsp_diagnostics` clean on all touched files.
- The parent plan's §0 supersession notice still points here, and this plan's §3 mismatch list reads as "fixed" in a follow-up commit/PR description (do not edit history).
- No code outside the three files in §6 changed under the cover of this plan.
