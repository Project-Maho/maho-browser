# Zen-Parity Transient Notifications Plan — 2026-06-04

**Date**: 2026-06-04
**Author**: Sisyphus
**Status**: Phase 1 landed (functional), but **visual layout is NOT Zen-parity**. See remediation plan: `zen-parity-transient-notifications-layout-remediation-2026-06-04.md`.
**Scope**: Bring Maho's transient toast surface to Zen Browser parity for the small, well-defined set of user actions where Zen actually shows a toast. NOT a global "every command emits a toast" system.

---

## §0 Superseded Assumption Notice (added 2026-06-04, post-Phase-1)

The original Phase 1 layout requirement in this document specified **top-center** placement (see §4.1 goal 4 and §5 Phase 1 "Behavior"). **That was wrong.** A subsequent audit of the actual Zen reference implementation found that Zen renders transient toasts in a **top-right** container by default, and only flips horizontal anchoring when the user has chosen a right-side sidebar layout (mirrored chrome). Zen does not center toasts horizontally in the content area under any layout.

**Treat the "top-center" requirement in this document as historical and non-authoritative for visual parity.** Phase 1 has shipped functionally (the primitive renders, animates, dismisses, and replaces correctly), but its anchor math (`(content_bounds.width() - view_size.width()) / 2`) and its command-bar-style chrome (`kMahoColorCommandBarBackground`, `kMahoColorCommandBarPanelBorder`) do not match Zen.

The follow-up plan `zen-parity-transient-notifications-layout-remediation-2026-06-04.md` documents the verified Zen behavior, the specific current Maho mismatch, and the remediation phases. Future implementation passes against this notification surface should follow the remediation plan, not §5 Phase 1 of this document.

The phase scoping (§2.1, §2.2, §6) and the callsite list (§10) remain authoritative. **Only the visual/positional contract for the primitive is superseded.**

---

## §1 Problem Statement

Maho currently ships a stub toast primitive (`MahoNotificationOverlay`) that only writes a `LOG(INFO)` line. Several high-value user actions (copy URL, bulk tab close, export favorites) complete silently with no UI confirmation. Zen Browser, our parity reference, gives the user immediate, transient confirmation for exactly this class of action via a shared `gZenUIManager.showToast(...)` primitive.

The risk in this work is over-scoping. A naive reading of "Zen parity" is "wrap every command action in a toast." That is wrong. Zen does **not** toast every command. Zen toasts a specific, intentional subset, and other surfaces (sidebar notifications, infobar exceptions, instructional overlays) handle different categories of feedback. This plan codifies that scoping decision.

---

## §2 Parity Findings (Source-of-Truth Summary)

The Zen reference codebase was audited for every `showToast` callsite plus adjacent notification surfaces. Findings:

### 2.1 Zen has exactly **12 `showToast` callsites**, organized into 4 user-facing categories

| Category | Zen actions | Maho parity decision |
|---|---|---|
| **Copy confirmations** | Copy current tab URL, copy any tab URL, copy link from context menu | **In scope, Phase 2** |
| **Bulk tab actions** | Close all tabs in space, close all tabs in folder, close duplicate tabs, unload tabs | **In scope, Phase 3** |
| **Export success** | Export bookmarks/favorites, export workspace data | **In scope, Phase 3** |
| **Rename success** | Rename folder/tab confirmation | **In scope, Phase 4 (deferred until rename UX lands)** |

### 2.2 Zen has **non-toast** notification surfaces that we are explicitly NOT folding into the toast primitive

- **Sidebar notification dot / banner** — used for sync conflicts, update available, integrity issues
- **Infobar exceptions** — used for permission prompts, restored-session errors
- **Onboarding completion overlays** — full-screen / inline, not transient toasts
- **Instructional hints** — first-run nudges and feature discovery; gated by "have I seen this" prefs

### 2.3 Categories with **no Zen equivalent** that we will NOT invent

- Background-tab-opened toast (Arc has it; Zen does not)
- Generic "command executed" toast for the command palette
- Per-keystroke shortcut acknowledgment toasts
- Corruption / recovery warnings (this stays in the existing recovery dialog path; toast is wrong UX for a destructive-recovery surface)

**Implication**: Phase 1 builds the primitive. Phases 2–4 wire it to a closed set of 5–7 callsites total. There is no "Phase 5: instrument every action."

---

## §3 Current Maho State

| File | State |
|---|---|
| `maho-chromium/browser/ui/notifications/maho_notification_overlay.h` | Stub class, single `Show(title, body)` method |
| `maho-chromium/browser/ui/notifications/maho_notification_overlay.cc` | Implementation logs to `LOG(INFO)` only; no UI |
| `maho-chromium/browser/BUILD.gn` (lines 309–310) | Sources already listed in browser target — no GN change needed for Phase 1 wiring |
| `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc` | Command palette dispatch; copy/bulk/export entry points live here |
| `maho-chromium/browser/ui/context_menu/maho_tab_context_menu.cc` | Per-tab "Copy URL", "Close other tabs", "Close tabs to right" |
| `maho-chromium/browser/ui/context_menu/maho_address_bar_context_menu.cc` | Address bar "Copy URL" |
| `maho-chromium/browser/ui/context_menu/maho_favorites_context_menu.cc` | Favorites "Copy link", export-related entries |
| `maho-chromium/browser/ui/context_menu/maho_space_context_menu.cc` | Space-level bulk actions (close all in space) |
| `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` | Partial rename plumbing; rename-success path is incomplete |

No `BUILD.gn` exists at `browser/ui/notifications/`; the two stub files are inlined into `browser/BUILD.gn`. This stays as-is.

---

## §4 Goals and Non-Goals

### 4.1 Goals
1. Replace the `MahoNotificationOverlay` stub with a real Views-based transient toast (anchored to the browser frame, auto-dismissing, queueable).
2. Wire **only** the 4 parity categories from §2.1 to that primitive.
3. Keep call surface tiny and explicit: callers pass a localized title plus an optional short body; no implicit "instrument every command" indirection.
4. Match Zen's UX feel: ~3s dismiss, single line preferred, slide+fade transition, top-of-content placement. **(SUPERSEDED: "top-of-content" was implemented as top-center; Zen actual is top-right with left-flip only in mirrored layouts. See §0 and the remediation plan.)**

### 4.2 Non-Goals
- Sidebar notification dot, infobar surface, onboarding overlays — separate features, separate plans.
- Generic post-command toast hook in `maho_command_action_handler.cc`. Each call is explicit at the leaf.
- Background-tab-opened toast (Arc-only, not Zen).
- Sound/haptic feedback.
- Cross-window queuing (per-`BrowserView` is sufficient).
- Action buttons inside toasts (Undo, etc.). Zen uses plain text toasts; we match.

---

## §5 Phased Implementation

### Phase 1 — Build the primitive (no callers wired yet)

> **SUPERSEDED for visual parity.** Phase 1 shipped and is functionally correct (primitive renders, animates, dismisses, replaces, isolates per `BrowserView`). The anchor and chrome below are NOT Zen-parity and must be revised per `zen-parity-transient-notifications-layout-remediation-2026-06-04.md`. Do not treat the layout/style language in this section as the contract going forward.

**File**: `maho-chromium/browser/ui/notifications/maho_notification_overlay.{h,cc}`

Rewrite the stub into a real Views-based toast.

Required surface:
```cpp
class MahoNotificationOverlay : public views::WidgetObserver {
 public:
  explicit MahoNotificationOverlay(BrowserView* browser_view);
  ~MahoNotificationOverlay() override;

  // Primary entry point. Title is the only required field. Body is optional.
  // Calls coalesce: if a toast is already visible, the new one replaces it
  // (Zen behavior: latest action wins; queueing is overkill for our scope).
  void Show(const std::u16string& title, const std::u16string& body = u"");

 private:
  void StartDismissTimer();
  void OnDismissTimerFired();
  void AnimateIn();
  void AnimateOut();
  // views::WidgetObserver
  void OnWidgetDestroying(views::Widget* widget) override;

  raw_ptr<BrowserView> browser_view_;
  std::unique_ptr<views::Widget> widget_;
  base::OneShotTimer dismiss_timer_;
  SEQUENCE_CHECKER(sequence_checker_);
};
```

Behavior:
- ~~Anchored to top-center of the `BrowserView` content area (not over the sidebar).~~ **SUPERSEDED. Correct anchor: top-right of the content area, with horizontal flip only when the sidebar/chrome is mirrored to the right side. See remediation plan.**
- Auto-dismiss after **3000 ms** (Zen default).
- Slide-in from above + fade-in over **180 ms**; slide-out + fade-out over **160 ms**. **(Animation timings are roughly fine; the slide axis/origin may need to track the right-edge anchor. See remediation plan §Animation.)**
- Replacing a live toast cancels the dismiss timer and resets it for the new content.
- `Show` is sequence-checked; toast lifecycle is UI-thread only.

Owner: created lazily by `BrowserView` (or whichever `MahoBrowserController` analog already holds per-window UI state — confirm during execution; do NOT introduce a new singleton).

**Phase 1 acceptance**:
- [ ] `MahoNotificationOverlay::Show(u"Test", u"Body")` from a temporary debug callsite renders a real toast on screen.
- [ ] Two rapid calls show the second message and reset the dismiss timer.
- [ ] `python3 maho-chromium/build/scripts/build_maho.py` exits 0.
- [ ] `lsp_diagnostics` clean on both files.
- [ ] No `LOG(INFO)` left from the stub.

---

### Phase 2 — Copy confirmations

Wire the primitive to copy actions. Strings are localized via existing GRD; if a string ID is missing, add it under the existing Maho IDS_ namespace (do **not** invent an ad-hoc string table).

**Callsites**:
- `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc` — command palette "Copy current tab URL" branch.
- `maho-chromium/browser/ui/context_menu/maho_tab_context_menu.cc` — per-tab "Copy URL".
- `maho-chromium/browser/ui/context_menu/maho_address_bar_context_menu.cc` — "Copy URL".
- `maho-chromium/browser/ui/context_menu/maho_favorites_context_menu.cc` — "Copy link".

Pattern at each callsite (explicit, not via a generic post-command hook):
```cpp
ui::ScopedClipboardWriter(...).WriteText(...);
GetMahoNotificationOverlay()->Show(
    l10n_util::GetStringUTF16(IDS_MAHO_TOAST_LINK_COPIED));
```

Toast copy:
- Title: `"Link copied"` (single string for all four entry points; Zen uses one string).
- No body.

**Phase 2 acceptance**:
- [ ] All four copy paths show the toast.
- [ ] Clipboard content is correct on all four (regression check).
- [ ] Toast does not appear when copy fails (e.g., empty selection); branch on the actual write outcome.

---

### Phase 3 — Bulk tab actions and export success

**Bulk tab callsites**:
- `maho-chromium/browser/ui/context_menu/maho_space_context_menu.cc` — "Close all tabs in space".
- `maho-chromium/browser/ui/context_menu/maho_tab_context_menu.cc` — "Close other tabs", "Close tabs to right", "Close duplicate tabs".
- `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc` — equivalent command palette entries (only for the same actions; do NOT add toasts to single-tab close, navigate, or focus commands).

Toast copy (mirrors Zen):
- `"Closed N tabs"` (singular: `"Closed 1 tab"`). Use `l10n_util::GetPluralStringFUTF16`.

**Export callsites**:
- `maho-chromium/browser/ui/context_menu/maho_favorites_context_menu.cc` — favorites/bookmarks export success branch.
- Any export entry point in `maho_command_action_handler.cc` (audit during execution; if none exist yet, leave a TODO comment, do NOT fabricate).

Toast copy: `"Exported"` with body `"<filename>"` for export, matching Zen's two-line export toast.

**Phase 3 acceptance**:
- [ ] Closing all tabs in a space shows `"Closed N tabs"` with correct plural.
- [ ] Cancelled or no-op bulk closes (zero tabs affected) do NOT toast.
- [ ] Export success toast fires only after the file is actually written; failures route to existing error dialog, not the toast.

---

### Phase 4 — Rename success (deferred until rename UX is complete)

Currently `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` has partial rename plumbing. Once the rename commit path is finalized in a separate effort:

- Wire toast at the rename-commit callback only (not on each keystroke, not on cancel).
- Toast copy: `"Renamed"`. Optional body: the new name (truncated to ~40 chars).

This phase is intentionally last and may slip without blocking Phases 1–3.

**Phase 4 acceptance** (when unblocked):
- [ ] Rename commit fires the toast.
- [ ] Rename cancel (Esc, blur-without-change) does not toast.
- [ ] Rename failure (validation reject) does not toast; it surfaces via the existing inline-edit error path.

---

## §6 Out of Scope / Deferred

These are explicitly **not** in this plan and should not be added during execution:

| Surface | Why deferred / out of scope |
|---|---|
| Background-tab-opened toast | Arc behavior, not Zen; no parity claim |
| Instructional first-run hints | Different surface (overlay/coachmark), not a toast |
| Onboarding completion confirmation | Full-screen / inline overlay path, not a toast |
| Corruption / recovery warnings | Destructive-action context demands a modal; toast is wrong UX |
| Sidebar notification dot | Persistent, not transient; separate subsystem |
| Permission / sync infobar | Already an infobar; toasts would duplicate |
| Generic command-palette "executed" toast | Zen does not do this; would create noise |
| Undo affordance inside toasts | Zen toasts have no actions; out of parity scope |

If any of these are requested later, write a separate plan. Do not graft them onto this primitive.

---

## §7 Verification Plan

### 7.1 Build and static checks
- `python3 maho-chromium/build/scripts/build_maho.py` exits 0 after each phase.
- `lsp_diagnostics` clean on every file edited.
- No new GN edits required for Phases 1–3 (sources already listed in `browser/BUILD.gn`); if Phase 4 needs new files, request explicit user permission per `maho-chromium/AGENTS.md`.

### 7.2 Manual smoke
Run the built `out/Default/Chromium.app/Contents/MacOS/Chromium` and verify per phase:
- **Phase 1**: temporary debug callsite renders, animates in/out, dismisses after ~3s, second call replaces first.
- **Phase 2**: Cmd-L copy, address-bar context-menu copy, tab context-menu copy, favorites context-menu copy — all show `"Link copied"`. Pasted clipboard contains the URL on all four.
- **Phase 3**: Space context menu → close all → toast shows correct count. Favorites export → toast shows filename.
- **Phase 4**: Once rename is wired, rename a sidebar tab → toast on commit, silence on cancel.

### 7.3 Regression guards
- Verify clipboard write still happens even if toast construction fails (toast is best-effort, not on the critical path).
- Verify rapid repeated copies (5x within 1s) do not stack widgets or leak; only one widget should exist at any time.
- Verify destroying the `BrowserView` while a toast is live does not crash (`OnWidgetDestroying` cleanup).

---

## §8 Risks and Mitigations

| Risk | Mitigation |
|---|---|
| Scope creep into "every command toasts" | §2.1 enumerates the closed set; §6 lists explicit deferrals |
| Toast singleton across windows causes wrong-window display | One overlay per `BrowserView`; do NOT make it a profile- or process-global singleton |
| String-table sprawl | Reuse `IDS_MAHO_TOAST_*` namespace; one string per category; no per-callsite strings |
| Localization gaps | All strings via `l10n_util`; plural strings via `GetPluralStringFUTF16` |
| Animation jank on low-end / under load | 180ms/160ms is short enough to be cheap; no compositor-heavy effects |
| Toast obscures critical UI (omnibox dropdown, find bar) | Top-center placement above content area, below toolbar; verify with omnibox open |
| Phase 4 rename UX still in flux | Phase 4 is explicitly deferrable; Phases 1–3 ship independently |

---

## §9 Execution Order

1. **Phase 1** (primitive) — single-session work, no callers wired. Land and verify before touching any context menus.
2. **Phase 2** (copy) — four files, identical pattern. Easiest win; high user-visible impact.
3. **Phase 3** (bulk + export) — plural strings, count math, export success branch checks.
4. **Phase 4** (rename) — only after the separate rename UX work merges.

Do not parallelize Phase 2 and Phase 3 in the same commit; the copy paths are a clean review unit.

---

## §10 Affected Files (Summary)

**Phase 1 (rewrite)**:
- `maho-chromium/browser/ui/notifications/maho_notification_overlay.h`
- `maho-chromium/browser/ui/notifications/maho_notification_overlay.cc`

**Phase 2**:
- `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc`
- `maho-chromium/browser/ui/context_menu/maho_tab_context_menu.cc`
- `maho-chromium/browser/ui/context_menu/maho_address_bar_context_menu.cc`
- `maho-chromium/browser/ui/context_menu/maho_favorites_context_menu.cc`

**Phase 3**:
- `maho-chromium/browser/ui/context_menu/maho_space_context_menu.cc`
- `maho-chromium/browser/ui/context_menu/maho_tab_context_menu.cc` (additional bulk branches)
- `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc` (bulk + export branches)
- `maho-chromium/browser/ui/context_menu/maho_favorites_context_menu.cc` (export branch)

**Phase 4 (deferred)**:
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`

**No GN changes expected**. If GN edits become necessary, stop and request explicit user permission per `AGENTS.md`.

---

## §11 Done Definition

Phases 1–3 are done when:
- All Phase 2 and Phase 3 callsites in §10 fire toasts on success.
- All §6 deferrals remain unimplemented (no scope creep).
- `build_maho.py` exits 0.
- Manual smoke from §7.2 passes for the relevant phases.
- `lsp_diagnostics` clean on every edited file.

Phase 4 is done independently when the rename UX work lands and the rename-commit toast is wired with the cancel/failure exclusions intact.
