# CMD+SHIFT+Y Opens History

## TL;DR
> **Summary**: Add `CMD+SHIFT+Y` as an additional macOS accelerator for Chromium's existing `IDC_SHOW_HISTORY` command, preserving current `CMD+Y` behavior and reusing the existing `chrome://history/` dispatch path.
> **Deliverables**:
> - `CMD+SHIFT+Y` opens History in the macOS Chromium fork app.
> - Focused regression coverage proves both `CMD+Y` and `CMD+SHIFT+Y` resolve to `IDC_SHOW_HISTORY`.
> - Verification notes capture targeted test/build/manual QA outcomes.
> **Effort**: Quick
> **Parallel**: NO
> **Critical Path**: Task 1 -> Task 2 -> Final Verification

## Context
### Original Request
User asked for a plan to make `CMD+SHIFT+Y` show History in the Chromium fork app.

### Research Summary
- `chromium/src/chrome/browser/ui/cocoa/accelerators_cocoa.mm` currently maps `IDC_SHOW_HISTORY` to `ui::EF_COMMAND_DOWN` + `ui::VKEY_Y`.
- No `CMD+SHIFT+Y` mapping for `IDC_SHOW_HISTORY` was found in `maho-chromium` or upstream `chromium/src` shortcut files.
- `chromium/src/chrome/browser/ui/browser_command_controller.cc` dispatches `IDC_SHOW_HISTORY` to `ShowHistory(...)`.
- `chromium/src/chrome/browser/ui/chrome_pages.cc` opens `chrome://history/` in normal mode and shows the Incognito history disclaimer off-the-record.
- `chromium/src/chrome/browser/global_keyboard_shortcuts_mac.mm` matches exact key/modifier combinations through main-menu or hidden shortcut data.

### Gap Review
- Risk: `AcceleratorsCocoa` stores accelerators keyed by command id, so adding a second mapping for the same command may overwrite the menu-displayed accelerator. The implementation must verify how duplicate command ids behave before choosing final placement.
- Risk: macOS menu tests expect accelerator table entries to have corresponding menu items. If a duplicate accelerator is added to `accelerators_cocoa.mm`, existing tests may fail because History menu displays only one equivalent.
- Resolution: Prefer adding `CMD+SHIFT+Y` to hidden shortcut data if duplicate command-id accelerator map behavior is unsuitable, while keeping `CMD+Y` as the visible menu accelerator.

## Work Objectives
### Core Objective
Pressing `CMD+SHIFT+Y` in the macOS Chromium fork app opens the same History surface as the existing History command.

### Must Have
- `CMD+SHIFT+Y` resolves to `IDC_SHOW_HISTORY`.
- Existing `CMD+Y` continues to resolve to `IDC_SHOW_HISTORY`.
- No History UI behavior changes.
- No GN workaround edits if build/test fails after GN regen or later.

### Must NOT Have
- Do not replace `CMD+Y` unless explicitly requested later.
- Do not implement a Maho-specific History page.
- Do not edit `chromium/src/**/BUILD.gn`, `maho-chromium/**/BUILD.gn`, or `chrome/test/BUILD.gn` to bypass build failures.
- Do not use type-safety suppressions or broad refactors.

## Verification Strategy
> ZERO HUMAN INTERVENTION - all verification is agent-executed where environment permits.
- Test decision: tests-after. This is a narrow shortcut registration change and existing behavior is already known.
- Primary tests:
  - Focused unit/browser test for shortcut resolution.
  - Targeted build/test command for the touched test target if available.
- Manual QA:
  - Launch built app if available, press `CMD+SHIFT+Y`, verify active tab URL is `chrome://history/`.
  - Also press `CMD+Y` to verify existing shortcut still works.
- Evidence:
  - `.omo/evidence/task-1-cmd-shift-y-history.txt`
  - `.omo/evidence/task-2-cmd-shift-y-history.txt`
  - `.omo/evidence/final-cmd-shift-y-history.txt`

## Execution Strategy
### Parallel Execution Waves
Wave 1: Task 1
Wave 2: Task 2
Wave 3: Final Verification

### Dependency Matrix
| Task | Depends On | Blocks |
| --- | --- | --- |
| 1. Add shortcut mapping | None | 2, Final |
| 2. Add focused regression coverage | 1 | Final |
| Final Verification | 1, 2 | None |

## TODOs
- [ ] 1. Add `CMD+SHIFT+Y` History Shortcut

  **What to do**:
  - Inspect `AcceleratorsCocoa` construction in `chromium/src/chrome/browser/ui/cocoa/accelerators_cocoa.mm` to confirm duplicate command-id behavior.
  - If duplicate command ids are supported without losing `CMD+Y`, add:
    - command: `IDC_SHOW_HISTORY`
    - modifiers: `ui::EF_COMMAND_DOWN | ui::EF_SHIFT_DOWN`
    - key: `ui::VKEY_Y`
  - If duplicate command ids overwrite the visible menu accelerator, add the shortcut to `GetShortcutsNotPresentInMainMenu()` in `chromium/src/chrome/browser/global_keyboard_shortcuts_mac.mm` instead:
    - `{true, true, false, false, kVK_ANSI_Y, IDC_SHOW_HISTORY}`
  - Keep `CMD+Y` as the visible menu accelerator.

  **Must NOT do**:
  - Do not remove the existing `CMD+Y` accelerator.
  - Do not change `ShowHistory`, History WebUI, or command palette behavior.
  - Do not edit GN files to force test/build success.

  **Parallelization**: Can Parallel: NO | Wave 1 | Blocks: Task 2, Final | Blocked By: None

  **References**:
  - Pattern: `chromium/src/chrome/browser/ui/cocoa/accelerators_cocoa.mm:56` - current visible History accelerator is `CMD+Y`.
  - Pattern: `chromium/src/chrome/browser/global_keyboard_shortcuts_mac.mm:121` - hidden macOS shortcut list for shortcuts not present in main menu.
  - API: `chromium/src/chrome/browser/ui/browser_command_controller.cc:1149` - `IDC_SHOW_HISTORY` dispatch target.
  - API: `chromium/src/chrome/browser/ui/chrome_pages.cc:337` - concrete History behavior.

  **Acceptance Criteria**:
  - [ ] Static inspection shows `CMD+SHIFT+Y` maps to `IDC_SHOW_HISTORY`.
  - [ ] Static inspection shows `CMD+Y` still maps to `IDC_SHOW_HISTORY`.
  - [ ] No source outside shortcut/test files is changed.

  **QA Scenarios**:
  ```text
  Scenario: Happy path static mapping
    Tool: bash
    Steps: grep the edited shortcut file for IDC_SHOW_HISTORY and the CMD+SHIFT+Y modifier/key tuple.
    Expected: One mapping exists for CMD+SHIFT+Y -> IDC_SHOW_HISTORY, and existing CMD+Y remains.
    Evidence: .omo/evidence/task-1-cmd-shift-y-history.txt

  Scenario: Scope guard
    Tool: bash
    Steps: git diff --name-only and inspect changed files.
    Expected: Only shortcut/test files are changed; no GN files are changed.
    Evidence: .omo/evidence/task-1-cmd-shift-y-history-scope.txt
  ```

  **Commit**: NO | Message: `fix(mac): add history shortcut` | Files: shortcut file chosen by implementation

- [ ] 2. Add Focused Regression Coverage

  **What to do**:
  - If implementation uses `GetShortcutsNotPresentInMainMenu()`, add or update `chromium/src/chrome/browser/global_keyboard_shortcuts_mac_unittest.mm` to assert `Command+Shift+Y` resolves to `IDC_SHOW_HISTORY`.
  - Also assert `Command+Y` still resolves to `IDC_SHOW_HISTORY`, either in the same test or via the menu accelerator path if the helper supports it.
  - If implementation uses `accelerators_cocoa.mm`, add or update `chromium/src/chrome/browser/ui/cocoa/accelerators_cocoa_browsertest.mm` or a lower-level test that checks the accelerator map without relying on a single menu item per command.

  **Must NOT do**:
  - Do not weaken existing accelerator/menu consistency tests.
  - Do not remove assertions that protect main menu accelerator behavior.

  **Parallelization**: Can Parallel: NO | Wave 2 | Blocks: Final | Blocked By: Task 1

  **References**:
  - Pattern: `chromium/src/chrome/browser/global_keyboard_shortcuts_mac_unittest.mm:95` - helper pattern for resolving a key/modifier tuple to a command.
  - Pattern: `chromium/src/chrome/browser/ui/cocoa/accelerators_cocoa_browsertest.mm:105` - existing macOS accelerator consistency tests.
  - Build listing: `chromium/src/chrome/test/BUILD.gn:7841` includes `global_keyboard_shortcuts_mac_unittest.mm`; do not edit unless explicitly required for a new file.

  **Acceptance Criteria**:
  - [ ] Test fails without the shortcut change and passes with it, where practical to verify.
  - [ ] Test explicitly names `IDC_SHOW_HISTORY`.
  - [ ] Test covers `CMD+SHIFT+Y`.
  - [ ] Existing `CMD+Y` behavior remains covered by test or static verification.

  **QA Scenarios**:
  ```text
  Scenario: Unit regression
    Tool: bash
    Steps: run the narrow Chromium test binary/filter for GlobalKeyboardShortcuts or AcceleratorsCocoa, depending on touched test.
    Expected: New regression assertion passes.
    Evidence: .omo/evidence/task-2-cmd-shift-y-history.txt

  Scenario: Existing shortcut protection
    Tool: bash
    Steps: run or inspect test coverage for CMD+Y -> IDC_SHOW_HISTORY after the change.
    Expected: CMD+Y remains mapped to IDC_SHOW_HISTORY.
    Evidence: .omo/evidence/task-2-cmd-y-history.txt
  ```

  **Commit**: NO | Message: `test(mac): cover history shortcut` | Files: focused test file

## Final Verification Wave
- [ ] F1. Plan Compliance Audit
  - Verify the implementation reused `IDC_SHOW_HISTORY`.
  - Verify no History UI, command palette behavior, or GN workaround edits were introduced.

- [ ] F2. Code Quality Review
  - Review the diff for minimality and Chromium style.
  - Confirm includes are unchanged unless required.

- [ ] F3. Test Execution
  - Run the focused test target/filter that covers the changed shortcut.
  - If the full target cannot run due to existing workspace/build-lock issues, record exact command and failure reason.

- [ ] F4. Manual QA
  - If a built app is available, launch it with a clean/test profile.
  - Press `CMD+SHIFT+Y`; expected active tab is `chrome://history/`.
  - Press `CMD+Y`; expected active tab is still `chrome://history/`.
  - In Incognito, press `CMD+SHIFT+Y`; expected behavior matches existing `ShowHistory` off-the-record disclaimer.

## Commit Strategy
Do not commit automatically. Keep changes staged/unstaged for user review unless explicitly asked to commit.

## Success Criteria
- `CMD+SHIFT+Y` opens History through `IDC_SHOW_HISTORY`.
- `CMD+Y` still opens History.
- Focused regression coverage exists and passes, or an environment/pre-existing failure is recorded precisely.
- Manual QA through the app surface is completed when a runnable app is available.
- No unrelated dirty worktree changes are modified or reverted.
