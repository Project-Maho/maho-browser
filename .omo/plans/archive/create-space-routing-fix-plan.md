# Implementation Plan: Route "New Space" to Native Inline Surface

## 0. Background — what's actually broken

User-visible behavior: clicking the sidebar footer `+` → "New Space" opens an **empty modal Widget** instead of the surface this session was tuning.

Root cause (verified directly, not via any agent):
- `maho_sidebar_footer_view.cc:455–463` — `HandleActionPopoverAction(kNewSpace)` calls
  `MahoSpaceCreateDialog::Open(browser_)`.
- `maho_space_create_dialog.cc:70–91` — `Open()` creates a fresh `views::Widget` (456×600 fixed)
  whose contents view is a `views::WebView` loading `chrome://maho-space-create/` (WebUI).
- The WebUI HTML at `maho-chromium/browser/resources/maho_space_create/space_create.html`
  references `app.js` and a custom element `<maho-space-create>` that currently render empty.

Wrong assumption from earlier in the session: that the live "New Space" path was the inline
native `MahoSidebarCreateSpaceView` triggered through `MahoSidebarView::ShowCreateSpaceSurface()`.
The inline surface is real native code and is what we improved this session, but **production
routing never reaches it** — the live entry is the modal+WebUI path.

Other production caller of the same dialog:
- `maho_command_action_handler.cc:385` — command-palette "Create space" action.

## 1. Goal

Make the user-visible "New Space" surface match `maho/reference_screenshots/create-space.jpeg`
without losing the work already invested in `MahoSidebarCreateSpaceView`.

## 2. Source of truth

| Layer | Authority |
|---|---|
| Visual target | `maho/reference_screenshots/create-space.jpeg` |
| Live entry points | `maho_sidebar_footer_view.cc:461`, `maho_command_action_handler.cc:385` |
| Existing native surface | `maho-chromium/browser/ui/views/sidebar/maho_sidebar_create_space_view.{cc,h}` |
| Existing modal+WebUI surface | `maho-chromium/browser/ui/views/space_create/maho_space_create_dialog.{cc,h}` + `browser/ui/webui/maho_space_create/*` + `browser/resources/maho_space_create/*` |

If any prior plan or agent claim conflicts with the file paths above, **the file paths win.**

## 3. Decision

**Route the production callers to `MahoSidebarView::ShowCreateSpaceSurface()` (inline native).**
Stop calling `MahoSpaceCreateDialog::Open()` from production sites.

Rationale:
- This session's native work already approximates the reference (multimodal: 7/10 element-by-element).
- The reference panel reads as a tall, full-height sidebar surface, not a fixed 456×600 modal.
- The native surface has the live theme integration via
  `MahoSpaceConfigDialog::OpenForCreate()` already wired.
- Fixing the WebUI side is a separate workstream (component bundle, layout CSS, parity with
  reference) of materially larger scope.
- One source of truth — no parallel parity work.

Out of scope explicitly (do not do in this plan):
- Building or polishing the WebUI dialog content.
- Removing or deleting `MahoSpaceCreateDialog`, the WebUI page handler, or its resources. Leave
  them in place but unreferenced from production callers. A separate cleanup task can decide
  whether to delete them later.

## 4. File-by-file changes

### `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc`
- In `HandleActionPopoverAction(MahoSidebarActionPopoverAction::kNewSpace)` (around line 460):
  - Remove `MahoSpaceCreateDialog::Open(browser_);`
  - Replace with logic that:
    1. Locates the ancestor `MahoSidebarContainerView` (same pattern already used for
       `kNewTab` in lines 471–486 of the same file).
    2. Resolves the `MahoSidebarView` inside the container (via the container's existing
       sidebar-view accessor used elsewhere) and calls `ShowCreateSpaceSurface()` on it.
  - Optionally: call `CloseActionPopover()` first (already done at line 457; do not duplicate).
- Remove the `#include "maho/browser/ui/views/space_create/maho_space_create_dialog.h"` if no
  other reference to it remains in this file. Otherwise leave.

### `maho-chromium/browser/ui/views/command/maho_command_action_handler.cc`
- Around line 385, replace `MahoSpaceCreateDialog::Open(browser);` with the equivalent native
  surface trigger:
  - Resolve `MahoSidebarView` from the active `Browser*` (use the same `BrowserView` traversal
    pattern as the footer view's `kNewTab` branch).
  - If found, call `ShowCreateSpaceSurface()`.
  - If not found (e.g., sidebar layout disabled), no-op for now and add a TODO; do **not** fall
    back to the dialog.
- Remove the dialog include if it becomes the last reference.

### `maho-chromium/browser/ui/views/sidebar/BUILD.gn`
- The `maho_sidebar_core` target currently lists
  `"//maho/browser/ui/views/space_create:maho_space_create_dialog"` as a dep
  (sidebar/BUILD.gn:209). Keep the dep for now — interactive tests in the sidebar tests
  source set still reference `MahoSpaceCreateDialog` indirectly. Cleanup is for a separate
  follow-up.

### `maho-chromium/browser/BUILD.gn`, `maho-chromium/browser/ui/views/command/BUILD.gn`
- Leave deps on `maho_space_create_dialog` in place. Removing them is a cleanup concern, not
  required for this fix.

### Tests
- `maho_sidebar_spaces_interactive_test.cc::OpenCreateSpaceSurface` already drives the popover
  click flow and asserts `is_create_space_mode_for_testing()` becomes true. After this routing
  change, the test should pass via the production code path rather than (currently) failing
  silently or being skipped. No test edits required initially.
- If any test explicitly drives `MahoSpaceCreateDialog::GetWidgetForTesting()` for the create
  flow (separate from `MahoSpaceConfigDialog`), audit and adjust to assert inline mode instead.
  Run a grep on `MahoSpaceCreateDialog::Get` / `MahoSpaceCreateDialog::Close` usages in tests
  before final commit.

## 5. Verification

### Build
- `python3 maho-chromium/build/scripts/build_maho.py --skip-rust` from repo root must complete
  with zero errors.

### Live click test (manual QA)
1. Launch `out/Default/Maho.app`.
2. Seed: navigate to `chrome://maho-test/?seed=2`.
3. Click footer `+` → action popover appears.
4. Click "New Space".
5. Expect: the **sidebar's body** transitions to the inline Create Space surface (the surface
   built this session). Top bar and footer hide. ESC returns to tabs mode. No new modal Widget
   spawns.
6. Capture screenshot. Compare with `maho/reference_screenshots/create-space.jpeg`.

### Command-palette path
1. Open command palette (Cmd-L or its equivalent).
2. Trigger the "Create space" / "New space" command.
3. Expect: same inline surface as above, not the modal Widget.

### Regression checks
- `MahoSidebarSpacesInteractiveTest.*` — the focused suite that already covers this flow.
- `MahoSidebarContractTest.*` — verify nothing about contract/state assertions broke.

## 6. Verification gates

| Gate | What | How | Blocking? |
|---|---|---|---|
| G1-BUILD | Build green | `build_maho.py --skip-rust` | YES |
| G2-CLICK | Footer `+` → New Space yields inline surface, no new Widget | Manual click + screenshot | YES |
| G3-CMD | Command-palette "Create space" yields inline surface | Manual | YES |
| G4-TESTS | Sidebar interactive + contract tests pass | Run target | YES |
| G5-VISUAL | Inline surface visually approximates reference | Side-by-side capture | FINAL |

## 7. Risks

| Risk | Mitigation |
|---|---|
| Command-palette callsite resolves browser → sidebar differently and the helper traversal returns null | Keep no-op fallback with TODO; verify on at least one live launch |
| Some tests currently exercised the dialog path and asserted dialog-specific behavior | Grep + audit before commit; if found, update tests to inline expectations |
| BUILD.gn deps on `maho_space_create_dialog` left in place create the impression the dialog is still live | Acceptable for this plan; leave for cleanup follow-up |
| Inline surface fills the entire sidebar — visually different from a 456×600 modal | This is the intent and matches reference better |

## 8. Rollback

If anything goes wrong post-routing-flip, revert the two callsite changes
(`maho_sidebar_footer_view.cc`, `maho_command_action_handler.cc`) — that fully restores the
previous behavior. No state migration required.

## 9. Commit strategy

Atomic, ordered:
1. `fix(sidebar): route New Space popover to inline native create-space surface`
2. `fix(command): route Create space command to inline native create-space surface`

Two commits so each callsite change can be reverted independently if needed.

## 10. Out of scope

- WebUI dialog redesign or polish.
- Deletion of `MahoSpaceCreateDialog` / its WebUI / Mojo / page handler.
- BUILD.gn dep cleanup.
- Hero illustration asset escalation (separate plan slot).
- Bigger sidebar layout changes outside the create-space body mode.
