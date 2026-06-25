# Space Switcher Context Menu Actions Implementation Plan

**Created:** 2026-05-25
**Status:** Draft, grounded in current main (commit `e2e5fbf`)
**Primary scope:**
- `maho-chromium/browser/ui/context_menu/maho_space_context_menu.cc`
- `maho-chromium/browser/ui/context_menu/maho_space_context_menu.h`
- `maho-chromium/browser/ui/context_menu/maho_space_context_menu_unittest.cc`
- `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc`

**Secondary scope (read-only references; do not redesign):**
- `maho-chromium/browser/ui/views/space_config/maho_space_config_dialog.{h,cc}`
- `maho-chromium/browser/ui/webui/maho_space_config/maho_space_config.mojom`
- `maho-chromium/browser/ui/webui/maho_space_config/maho_space_config_page_handler.{h,cc}`
- `.sisyphus/plans/theme-picker-architecture-decision.md` (ADR, permanent)

---

## 1. Objective

Make the three currently dead items in the sidebar footer space-switcher context menu functional:

1. **Rename Space** (`IDC_MAHO_SPACE_RENAME`)
2. **Change Space Icon** (`IDC_MAHO_SPACE_CHANGE_ICON`)
3. **Edit Theme Color** (`IDC_MAHO_SPACE_EDIT_THEME`)

These three rows currently render in the menu but are visually disabled and execute no action. The menu's other rows (New Folder, Share Space, Export/Import Theme, Delete Space, the inline icon and theme submenus) are already wired and out of scope for this plan unless a regression is observed.

---

## 2. Current-State Review (verified by direct reads this session)

### 2.1 Disabled state is hardcoded

`maho-chromium/browser/ui/context_menu/maho_space_context_menu.cc`, `IsCommandIdEnabled()` at lines 431–440:

```cpp
bool MahoSpaceContextMenu::IsCommandIdEnabled(int command_id) const {
  switch (command_id) {
    case IDC_MAHO_SPACE_RENAME:
    case IDC_MAHO_SPACE_CHANGE_ICON:
    case IDC_MAHO_SPACE_EDIT_THEME:
      return false;
    default:
      return true;
  }
}
```

This is the sole reason the three rows show greyed out. The menu model itself (`BuildMenuModel()`, lines 192–208) already adds them as ordinary command items.

### 2.2 ExecuteCommand branches are empty stubs

`maho_space_context_menu.cc`, `ExecuteCommand()` at lines 368–429:

```cpp
case IDC_MAHO_SPACE_RENAME:
  break;
case IDC_MAHO_SPACE_CHANGE_ICON:
  break;
case IDC_MAHO_SPACE_EDIT_THEME:
  break;
```

Even if the gating in 2.1 were flipped, nothing would happen on click. New Folder, Share, Export/Import Theme, the inline icon submenu, and the inline color submenu all post real work to the thread pool from this same switch.

### 2.3 Production / test mismatch

`maho_space_context_menu_unittest.cc` already asserts that all three commands are enabled:

```cpp
TEST(MahoSpaceContextMenuTest, AllCommandsEnabledRegardlessOfBrowser) {
  MahoSpaceContextMenu menu(nullptr, "space-1");
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_SPACE_RENAME));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_SPACE_CHANGE_ICON));
  EXPECT_TRUE(menu.IsCommandIdEnabled(IDC_MAHO_SPACE_EDIT_THEME));
  ...
}
```

So with the production code returning `false` for those three, this test must currently be failing or skipped in some configuration. The implementation pass should re-run this test and treat any green run on current main as a signal to investigate (test target may not be wired into the active suite).

### 2.4 Two menu invocation sites in the footer

`maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc` invokes the menu from two paths:

- `OnSpaceDotContextMenu()` at line 624, used when the user right-clicks a specific space dot. It passes a real `dot->space_id()` (line 630).
- `ShowContextMenuForViewImpl()` at line 358, used when the user right-clicks the footer area itself. It passes the placeholder string `"sidebar-spaces"` (line 366):

```cpp
MahoSpaceContextMenu context_menu(browser_, "sidebar-spaces");
```

`"sidebar-spaces"` is not a real space id. Any production code that resolves names, icons, colors, profiles, or theme JSON against this string will fail to find a matching record. The existing `GetSpaceName()` helper in the menu (lines 100–132) defaults to `"Space"` when no record matches, so today this is silently inert. Once Rename/Change Icon/Edit Theme do real writes against the supplied id, this placeholder becomes a correctness hazard.

### 2.5 Editing infrastructure already exists

`maho-chromium/browser/ui/views/space_config/maho_space_config_dialog.h` exposes:

```cpp
static void Open(Profile* profile,
                 gfx::NativeWindow parent_window,
                 const std::string& space_id,
                 maho_space_config::mojom::InitialFocus focus);
```

`maho_space_config.mojom` defines `InitialFocus { kName, kIcon, kColor, kProfile }` (lines 4–9) and the page handler implements:

- `UpdateName(string name)` (mojom line 37, handler line 214)
- `UpdateIcon(string icon)` (mojom line 38, handler line 224)
- `UpdateColor(string color_hex, string? theme_json)` (mojom line 39, handler line 280)

`UpdateColor` calls `RecolorSpaceOnPool` and then refreshes `MahoSpaceThemeState` and the `ColorProviderManager` cache, mirroring what the menu's existing inline color submenu does via `RecolorSpaceOnPool` directly (`maho_space_context_menu.cc` lines 170–182).

### 2.6 Theme picker ADR (permanent)

`.sisyphus/plans/theme-picker-architecture-decision.md` (2026-05-22) makes the theme picker permanent at `chrome://maho-space-create`, hosted by `MahoSpaceThemePickerDialog::Open`. The ADR also says `maho_space_config` is for "editing existing space settings only" and explicitly forbids merging theme picking into it.

This produces a real ambiguity for the **Edit Theme Color** menu item, which is addressed as a validation checkpoint in §3.3 below. Do not pre-decide the path; resolve it with the user during implementation.

---

## 3. Design Decisions

### 3.1 Rename Space

Wire `IDC_MAHO_SPACE_RENAME` to `MahoSpaceConfigDialog::Open(profile, parent_window, space_id_, InitialFocus::kName)`.

Rationale: the config dialog already exists, supports `kName` focus, and `UpdateName` is implemented end to end. Building a separate native rename inline editor would duplicate logic.

### 3.2 Change Space Icon

Wire `IDC_MAHO_SPACE_CHANGE_ICON` to `MahoSpaceConfigDialog::Open(..., InitialFocus::kIcon)`.

Rationale: same as 3.1. Note that the menu already has an inline 8-emoji icon submenu (commands `IDC_MAHO_SPACE_ICON_HOME` through `IDC_MAHO_SPACE_ICON_LIGHTNING`, lines 394–409); the new dialog path is the "open the full picker" affordance, which is the user expectation when clicking the top-level "Change Space Icon" row rather than picking an inline emoji.

### 3.3 Edit Theme Color (VALIDATION CHECKPOINT)

This item has two plausible implementations and the implementer must confirm with the user before coding:

**Option A: Open `MahoSpaceConfigDialog` with `InitialFocus::kColor`.**
- Symmetric with Rename and Change Icon.
- Reuses the existing `UpdateColor` page handler path.
- The mojom enum already defines `kColor`.
- Tension: the theme picker ADR says `chrome://maho-space-create` is the canonical theme picker surface and forbids "merging the picker into `maho_space_config`." Whether `InitialFocus::kColor` invokes the full picker UI inside `maho_space_config`, a simple swatch-only editor, or something else, is not visible from the files read in this plan.

**Option B: Open `MahoSpaceThemePickerDialog::Open(...)` directly.**
- Aligns with the ADR's "single picker production URL" rule.
- Requires an existing or new entry point that supports editing an existing space (versus the create-flow callback signature). Whether such an entry point already exists is not verified here.

**Required actions before coding this item:**

1. Read `maho-chromium/browser/resources/maho_space_config/` to determine what `InitialFocus::kColor` actually renders today.
2. Read `maho-chromium/browser/ui/views/space_create/maho_space_theme_picker_dialog.{h,cc}` to determine whether it supports an "edit existing space" callback, and what call site (if any) currently uses that mode.
3. Confirm with the user which option is canonical for the menu item, with the ADR in hand.

Do not silently pick a path. The plan treats both as live until the checkpoint resolves.

### 3.4 Footer "empty area" right-click (`"sidebar-spaces"`)

`ShowContextMenuForViewImpl()` passes a placeholder space id. Once the three new actions write against the supplied id, this will misroute. Two possible fixes:

1. Replace `"sidebar-spaces"` with the currently active space id (read from the same model the footer already gets in `Update()`, where `model.active_space_index` and `model.space_ids` are available).
2. Suppress the three new items entirely when the menu is opened from this empty-area path, by giving the menu a "no specific space" mode flag.

Recommendation: option 1 (use the active space id). This keeps menu semantics uniform across both invocation sites and removes a special-case branch. Confirm with the user during implementation if option 2 is preferred for UX reasons.

### 3.5 Sequencing and `event_flags`

`ExecuteCommand` already declares `(void)event_flags;`. The new branches do not need to consume modifier keys; preserve the discard.

All three new branches must obtain the parent `gfx::NativeWindow` from `browser_->window()->GetNativeWindow()` (the same call already used by `ExportTheme()` at line 302) and the `Profile*` from `browser_->profile()`. If `browser_` is null (the unit test passes a null browser), early-return without crashing. The existing `ShareSpace()` path lacks a null-browser guard; the new branches must not regress that property.

---

## 4. Phased Implementation

### Phase 0: Validation checkpoint
- Resolve §3.3 with the user.
- Resolve §3.4 with the user (active-space id vs. suppression).
- Confirm whether the failing/skipped unit test in §2.3 is currently green; if so, find out why (build target wiring) before relying on it as a regression net.

### Phase 1: Footer call-site fix
- Update `ShowContextMenuForViewImpl()` in `maho_sidebar_footer_view.cc` to pass the active space id (or implement the agreed alternative from §3.4).
- Verify that the dot-click path (`OnSpaceDotContextMenu`) is unchanged.

### Phase 2: Wire Rename and Change Icon
- Add the dialog include and call sites in `maho_space_context_menu.cc`.
- Drop the `IDC_MAHO_SPACE_RENAME` and `IDC_MAHO_SPACE_CHANGE_ICON` rows from the disabled list in `IsCommandIdEnabled()`.
- Keep `IDC_MAHO_SPACE_EDIT_THEME` disabled until Phase 3 lands.

### Phase 3: Wire Edit Theme Color
- Implement the option chosen in Phase 0.
- Drop `IDC_MAHO_SPACE_EDIT_THEME` from the disabled list.

### Phase 4: Tests
- Update `maho_space_context_menu_unittest.cc` so the existing `AllCommandsEnabledRegardlessOfBrowser` assertion passes against production.
- Add tests covering null-browser execution for all three new commands (mirroring the existing `Execute*WithNullBrowserDoesNotCrash` style at lines 164–187).

### Phase 5: Manual QA
- Right-click an existing space dot. Verify all three rows are enabled, click each, confirm the dialog opens focused on the correct field.
- Right-click the footer empty area. Verify the three rows act on the active space (or the agreed alternative).
- Rename, change icon, change color, close the dialog, reopen the menu, confirm changes persist.

---

## 5. File-by-File Changes

### `maho-chromium/browser/ui/context_menu/maho_space_context_menu.cc`

- Add includes: `maho/browser/ui/views/space_config/maho_space_config_dialog.h`, `maho/browser/ui/webui/maho_space_config/maho_space_config.mojom-shared.h`, `chrome/browser/profiles/profile.h`. (Confirm exact include paths against the existing dialog .cc.)
- In `ExecuteCommand`, replace the empty `IDC_MAHO_SPACE_RENAME` branch with a call that:
  1. Returns early if `browser_` is null.
  2. Calls `MahoSpaceConfigDialog::Open(browser_->profile(), browser_->window()->GetNativeWindow(), space_id_, maho_space_config::mojom::InitialFocus::kName);`.
- Repeat for `IDC_MAHO_SPACE_CHANGE_ICON` with `kIcon`.
- For `IDC_MAHO_SPACE_EDIT_THEME`, implement the option chosen at the §3.3 checkpoint.
- In `IsCommandIdEnabled`, remove the three case labels so they fall through to `return true`.

### `maho-chromium/browser/ui/context_menu/maho_space_context_menu.h`

- No new methods are required. Confirm there is no need to expose any internal state to tests beyond what already exists.

### `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc`

- In `ShowContextMenuForViewImpl()` (line 358), replace the literal `"sidebar-spaces"` with the resolved active space id from the footer's last `Update()` model, or suppress the three rows per §3.4.

### `maho-chromium/browser/ui/context_menu/maho_space_context_menu_unittest.cc`

- The existing assertions at lines 145–155 already require enabled state; they should pass automatically once production is fixed. Re-verify after Phase 3.
- Add tests:
  - `ExecuteRenameWithNullBrowserDoesNotCrash`
  - `ExecuteChangeIconWithNullBrowserDoesNotCrash`
  - `ExecuteEditThemeWithNullBrowserDoesNotCrash`
- These mirror the existing pattern at lines 164–187.

### Build files

No `BUILD.gn` changes are expected unless the include set in `maho_space_context_menu.cc` requires a new dep on the space_config target. Re-verify when the include is added.

---

## 6. Verification

### 6.1 Build
- `python3 maho-chromium/build/scripts/build_maho.py` from the workspace root.
- Do **not** edit any `BUILD.gn` to chase missing-target errors. Per the root and overlay AGENTS.md guides, GN failures during the implementation pass are reported to the user, not silently patched.

### 6.2 Unit tests
- Run the `maho_space_context_menu_unittest` target.
- Existing assertions to satisfy: `BuildMenuModelContains*`, `TopLevelOrderMatchesProduction`, `TopLevelHasExactlyThreeSeparators`, `AllCommandsEnabledRegardlessOfBrowser`, `Execute*WithNullBrowserDoesNotCrash`, `CommandIdValues`.
- New assertions: null-browser executes for the three new commands.

### 6.3 Diagnostics
- `lsp_diagnostics` on the four touched files; zero errors required.

### 6.4 Manual QA matrix
| Trigger | Expected |
|---|---|
| Right-click on space dot, click Rename | Config dialog opens with name field focused |
| Right-click on space dot, click Change Space Icon | Config dialog opens with icon field focused |
| Right-click on space dot, click Edit Theme Color | Result of §3.3 decision; verify focus and that the change persists |
| Right-click on empty footer area | All three rows act on the active space (or behave per the §3.4 decision) |
| Apply rename, close dialog, reopen menu | Title in dialog reflects the new name; sidebar reflects it |
| Apply icon change | Icon in the space dot updates without requiring browser restart |
| Apply color change | Sidebar tint refreshes immediately (existing `ColorProviderManager::ResetColorProviderCache` path) |

---

## 7. Risks

1. **Theme path ambiguity (§3.3).** Picking the wrong dialog for Edit Theme Color either contradicts the ADR (Option A, naive) or duplicates the page handler's existing `UpdateColor` flow (Option B, naive). Resolve at the checkpoint; do not guess.
2. **`"sidebar-spaces"` placeholder fan-out.** Any other call site that consumed this literal would also need updating. None are known from the files read this session, but a `grep` across the overlay should be done at implementation time.
3. **Unit test target wiring.** If `maho_space_context_menu_unittest.cc` is not actually built into the active test suite, the production/test mismatch survives unnoticed. The Phase 0 verification step exists to catch this.
4. **`browser_` null-safety.** Three new `ExecuteCommand` branches must early-return on null. The unit test enforces this for the existing branches and should be extended.
5. **Dialog modality and parent window.** `MahoSpaceConfigDialog::Open` takes a `gfx::NativeWindow`. If invoked from a footer that is detached from a `BrowserWindow` (theoretically possible during sidebar reparenting), `browser_->window()->GetNativeWindow()` could return a null handle. Verify behavior or guard.
6. **Theme refresh side effects.** The existing inline color submenu posts `RecolorSpaceOnPool` directly without resetting `ColorProviderManager`; the page handler's `UpdateColor` does reset it. If Option A is chosen, expect a subtle visual difference between the inline swatch row and the dialog path. This is a feature, not a regression.

---

## 8. Out of Scope

- New Folder, Share Space, Export Theme, Import Theme, Delete Space behavior (already wired or stubbed elsewhere).
- The 8-emoji inline icon submenu and 8-color inline theme submenu (already wired via `ChangeSpaceIconOnPool` and `RecolorSpaceOnPool`).
- The Delete Space confirmation dialog (`maho_space_context_menu.cc` line 378 currently logs a "pending" message; resolving that is its own task).
- The settings-side default search engine UI, command palette work, sidebar create-space form, or any non-context-menu surface.
- Mojom or Rust core changes. The plan reuses existing FFI and IPC paths only.
- GN restructuring. Implementation pass treats GN failures as a stop-and-report condition per the AGENTS.md rules.

---

## 9. Open Questions for Implementer

1. §3.3: Option A or Option B for Edit Theme Color?
2. §3.4: Replace `"sidebar-spaces"` with active space id, or suppress the three rows on the empty-area path?
3. Phase 0: Is `maho_space_context_menu_unittest` currently part of the build? If not, should we wire it before relying on it?
4. Should the new dialog opens be tracked through any analytics or shell event the project already logs for Share/Export/Import paths? (No evidence of such logging seen during file reads; confirm.)
