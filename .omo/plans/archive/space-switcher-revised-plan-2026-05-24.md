# Space Switcher Revised Plan

**Created:** 2026-05-24
**Supersedes:** `.sisyphus/plans/space-switcher-fix-plan-2026-05-23.md`
**Status:** Draft, grounded in the latest checked-in code
**Primary scope:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_space_dot_view.{cc,h}`, `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc`
**Secondary scope:** keyboard shortcut delivery for `next_space` / `prev_space` / `select_space_N` (focus path investigation, not accelerator-table edits)

---

## 1. Objective

Close the remaining user-visible gaps in the Maho space switcher now that the bridge unification work has landed. The active-space writer story is essentially settled at the C++ layer; what users still see broken is:

1. Active space dots never render their icon at rest.
2. Dots are too small to read as a navigation surface.
3. The keyboard shortcuts that should drive `next_space` / `prev_space` / `select_space_N` are not being delivered to the command action handler reliably, even though the handler itself is correct.

The plan keeps existing architectural decisions (browser-scoped active space via `MahoSpaceProfileBridge`, Rust core as data engine, theme picker ADR) intact.

---

## 2. Latest-Code Review Summary

The following reflects the current state of the tree, not aspirations.

- Canonical UI surface for the switcher is `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc` plus `maho_sidebar_space_dot_view.{cc,h}`. There is no Swift or WebUI surface in active use for this feature.
- Footer activation is already browser-scoped. Both the click handler and the keyboard navigation handler in `maho_sidebar_footer_view.cc` call `bridge->SwitchToSpace(browser_, dot->space_id())`.
- Command actions are already bridge-routed. In `maho_command_action_handler.cc`, `open_space:*`, `next_space`, `prev_space`, and `select_space_N` all call `bridge->SwitchToSpace(browser, ...)`. None of them call `maho_core_activate_space(...)` directly.
- The only remaining `maho_core_activate_space(...)` call in the Chromium tree is inside `maho_space_profile_bridge.cc` itself, which is the intended chokepoint.
- `maho_sidebar_state_adapter.cc` already prefers browser-scoped active space. `BuildFooterModel` overrides `active_space_index` from `bridge->GetActiveSpaceId(browser)` whenever it finds a matching core space id, with the global value only acting as a fallback.
- Active-dot icon rendering is still broken. `maho_sidebar_space_dot_view.cc` computes `const bool show_icon = is_hovered_ && has_icon_`, which means an active space at rest never shows its icon, only on hover.
- Dot sizing is still too small. The four size tiers are `2.0f / 2.4f / 2.9f / 3.8f`, which under-reads on real displays and was flagged as a visual issue in QA.
- Keyboard shortcut delivery is not yet diagnosed. `BrowserView::PreHandleKeyboardEvent` calls `MaybeHandleKeyEvent` before passing the event to `FocusManager`, but `ResolveActionForKeyEvent(const ui::KeyEvent&)` still has no caller in the tree. We do not have proof that this is an accelerator precedence problem. Treat it as an open focus-path / event-delivery investigation.

---

## 3. Already Landed (do not redo)

These pieces of the older May 23 plan are already in the tree and should not be re-scoped:

- **Bridge as single activation chokepoint.** `MahoSpaceProfileBridge::SwitchToSpace(Browser*, ...)` is the canonical user-switch entry. The only `maho_core_activate_space(...)` call left lives inside that bridge method.
- **Command actions migrated.** All four space command actions (`open_space:*`, `next_space`, `prev_space`, `select_space_N`) route through `bridge->SwitchToSpace(browser, ...)`.
- **Footer migrated to browser-scoped switching.** Both pointer activation and keyboard navigation in `maho_sidebar_footer_view.cc` already pass `browser_` into `SwitchToSpace`.
- **Reader prefers browser-scoped active space.** `maho_sidebar_state_adapter.cc::BuildFooterModel` overrides the active marker from the bridge's per-browser map when a matching core space id is present.

The writer-side and reader-side convergence work proposed in the prior plan's Phases 1 through 5 is therefore considered complete. No further bridge plumbing is part of this plan.

---

## 4. Remaining Work

### R1. Active dot icon never shows at rest (UI correctness) - **DONE**
- Successfully implemented in `maho_sidebar_space_dot_view.cc`.

### R2. Dot sizes are too small (visual) - **DONE**
- Successfully implemented in `maho_sidebar_space_dot_view.h`.

### R3. Space shortcuts are unreliable (event delivery, not accelerators) - **ACTIVE**
- Pressing the bound shortcuts does not consistently fire `next_space` / `prev_space` / `select_space_N` in native views focus locations (omnibox, sidebar).
- **Diagnosis**: `PreHandleKeyboardEvent` is only called for WebContents events. When native views have focus, events go to `views::FocusManager` directly. Since Maho shortcuts are not registered as accelerators, they are ignored.
- **Proposed Fix**: Override `OnKeyEvent` in `BrowserView` and register it as a pre-target handler on the widget in `AddedToWidget()`. This intercepts all key events before they go to focused views, resolving and executing Maho shortcuts globally.

---

## 5. Phases

### Phase A: Fix active-dot icon rendering (R1) - **DONE**

### Phase B: Resize dots to a readable scale (R2) - **DONE**

### Phase C: Diagnose the space shortcut delivery (R3) - **DONE**
- Detailed diagnosis is outlined in Section 4. We confirmed that events are cut off in native views focus locations because `BrowserView::PreHandleKeyboardEvent` is not triggered.

### Phase D: Apply the minimal shortcut fix (R3)
- Register `BrowserView` as a pre-target event handler in `BrowserView::AddedToWidget()`.
- Override `views::View::OnKeyEvent` in `BrowserView` to intercept and process Maho shortcuts.

---

## 6. Verification

### Automated

- `lsp_diagnostics` clean on `maho_sidebar_space_dot_view.{cc,h}` and `maho_sidebar_footer_view.cc` after Phases A and B.
- Build the Chromium target via `python3 maho-chromium/build/scripts/build_maho.py` after each editing phase.
- Run the existing nearest sidebar/footer interactive tests. Do not add new tests for R1 and R2 unless an existing test would have caught the regression. Add a regression test for R3 only if Phase D lands a code change that has a sensible test boundary.

### Manual

1. With multiple spaces created, verify the active space dot shows its icon at rest.
2. Hover an inactive space dot, verify icon appears, then move away, verify icon disappears.
3. Compare the dot row before and after the size change at 1x and 2x. Active versus inactive should read clearly.
4. With focus in the omnibox, in web contents, and in the sidebar, press the three shortcuts. Confirm they switch spaces from each focus location after Phase D.
5. Open two windows on different spaces, switch with footer click and with shortcut. Confirm browser-scoped active space stays correct in both windows.

---

## 7. Risks

### Risk 1: Re-doing already-landed work

The strongest risk on this plan is undoing or re-implementing the bridge migration. Phases A, B, C, and D explicitly do not touch `maho_space_profile_bridge.{h,cc}`, `maho_command_action_handler.cc`, or footer activation routing.

### Risk 2: Misreading the shortcut failure as accelerator precedence

The fact that `PreHandleKeyboardEvent` runs before `FocusManager` is observed but not proven causal. If we jump to accelerator-table or GN edits without Phase C, we are likely fixing the wrong layer and inviting regressions in unrelated key handling.

### Risk 3: Dot resize affects sidebar layout

Increasing dot size could overflow the footer row if other adjacent surfaces (overflow indicator, hover targets) are sized relative to the old constants. Mitigation: review surrounding layout constants in the footer view before committing the new tier.

### Risk 4: Icon-at-rest reveals theme contrast issues

Showing the active icon at rest may surface contrast bugs that hover masked. Mitigation: spot check across light, dark, and themed spaces during Phase A manual QA. Do not change icon coloring logic in this plan, file a separate issue if contrast is broken.

---

## 8. Review Notes

- This plan intentionally removes the bridge unification and command-handler migration phases from the older May 23 draft because the live code already has them.
- The plan does not edit Swift, WebUI, GN, or accelerator infrastructure as primary fixes. If Phase C concludes that any of those layers is the right one, that conclusion must be justified by the Phase C diagnosis, not assumed up front.
- "Active space at rest must show its icon" is treated as a correctness fix, not a visual change, because the current rendering hides identity information.
- Dot sizing change is values-only on purpose. Anything beyond constants (new shapes, ring outlines, spacing redesign) is out of scope here and should land through a dedicated visual plan if desired.
- The bridge-global `active_space_id_` and the per-browser map are kept in their current relationship. This plan does not propose changing either.
