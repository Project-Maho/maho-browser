# Arc Sidebar Folder Height Parity Plan

## TL;DR
> Summary:      Bring Maho's native sidebar folder rows back to Arc-like compact density by aligning folder rows to the existing 34 dp tab-row rhythm, tightening folder glyph sizing, and proving the result with a deterministic folder-seeded screenshot.
> Deliverables:
> - Folder row density tokens adjusted in the existing sidebar token path.
> - Focused layout regression for folder row height in an existing sidebar test file, with no GN edits.
> - Canonical wrapper build/test evidence.
> - Before/after visual QA evidence with `chrome://maho-test/?seed=2`.
> Effort:       Short
> Risk:         Medium - Chromium Views build/test cost, dirty worktree, and DnD tests that may contain synthetic 40 px folder-row geometry.

## Scope
### Must have
- Fix the current visual issue where folder rows look taller than normal tab rows.
- Keep the work narrow to native sidebar folder density.
- Use existing component boundaries:
  - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h`
  - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`
  - existing sidebar tests under `maho-chromium/browser/ui/views/sidebar/`
- Target folder row height: `34 dp`, matching `sidebar_layout::kRowHeightDp`.
- Target first-pass folder icon density:
  - `kFolderGlyphContainerSizeDp = 22`
  - `kFolderGlyphSizeDp = 14`
  - keep `kChevronSizeDp = 12`
  - keep `kFolderIndentDp = 14`
- Evaluate `kSectionSeparatorHeightDp = 22` separately after the row/glyph pass. If the seeded screenshot still shows the folder section as vertically bloated because of separator spacing, reduce it to `14 dp` and record the before/after measurement.
- Preserve drag/drop behavior, collapse/expand behavior, folder rename behavior, pinned vs normal folder behavior, and nested folder indentation.
- Capture real visual evidence from the built app with a profile seeded by `chrome://maho-test/?seed=2`.

### Must NOT have
- Do not reopen the broader Arc sidebar look-and-feel plan.
- Do not change top bar, search pill, favorites grid, footer, sidebar width, color mixer, or content panel unless a compile break directly requires a local include/test fix.
- Do not edit GN files as a workaround for build failures unless the user gives explicit approval for that exact GN edit.
- Do not delete or revert unrelated dirty worktree changes.
- Do not do a blind global replacement of hard-coded `40` in tests. Update only geometry that semantically represents actual folder row height.
- Do not declare success from build output alone. The final result requires screenshot evidence with visible folder rows.

## Verification Strategy
> Zero human intervention - all verification is agent-executed.
- Test decision: TDD + Chromium C++ Views/browser-test surface. Start by adding or updating a focused folder-row height assertion, confirm it detects the current `40 dp` behavior when feasible, then make the minimal token change.
- QA policy: every todo has agent-executed scenarios and evidence paths.
- Evidence: `.omo/evidence/task-<N>-arc-sidebar-folder-height.*` and `.omo/artifacts/arc-sidebar-folder-height/`.

Required commands:

```bash
python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target chrome
```

The regression should land in an already registered test file, preferably
`maho_sidebar_dnd_dispatch_unittest.cc`, so no GN edit is required. Build the
owning binary through the wrapper as well:

```bash
python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target unit_tests
chromium/src/out/Default/unit_tests --gtest_filter="SidebarDnDDispatchTest.*Folder*"
```

Do not bypass the wrapper for the first verification pass.

Required manual QA surface:

```bash
mkdir -p .omo/artifacts/arc-sidebar-folder-height
open -na chromium/src/out/Default/Maho.app --args --user-data-dir=/tmp/maho-folder-height-profile chrome://maho-test/?seed=2
screencapture -x .omo/artifacts/arc-sidebar-folder-height/maho-folder-height-window.png
```

If `open -na ... chrome://maho-test/?seed=2` does not navigate reliably, launch the app first and use the existing local Accessibility helper pattern from `.omo/artifacts/command-palette-parity/capture_command_palette.py` to focus the window, type the URL, and press Return. Record the exact command or helper invocation used. If the test WebUI or app binary is unavailable, report that limitation explicitly and do not claim visual completion.

## Execution Strategy
### Parallel Execution Waves
> Target 5-8 todos per wave. This is a narrow follow-up, so the final wave is intentionally smaller.

Wave 1 (no deps):
- Todo 1: Dirty-state and baseline evidence.
- Todo 2: Reference/test seam confirmation.

Wave 2 (after Wave 1):
- Todo 3: Add/update folder-row density regression and apply token fix.
- Todo 4: Review DnD/test geometry that may be coupled to the old 40 px row.

Wave 3 (after Wave 2):
- Todo 5: Build and targeted test verification.
- Todo 6: Seeded screenshot QA and measurement.

Critical path: Todo 1 -> Todo 3 -> Todo 5 -> Todo 6.

### Dependency Matrix
| Todo | Depends on | Blocks | Can parallelize with |
| --- | --- | --- | --- |
| 1 | none | 3, 5, 6 | 2 |
| 2 | none | 3, 4 | 1 |
| 3 | 1, 2 | 4, 5, 6 | none |
| 4 | 2, 3 | 5 | none |
| 5 | 3, 4 | 6 | none |
| 6 | 5 | final verification | none |

## Todos
- [x] 1. Capture dirty-state and current folder-density baseline
  What to do / Must NOT do:
  Record the current dirty state before touching files. Save current token values and, if the current app launches, a baseline seeded screenshot showing the tall folder rows. Do not reset or clean the worktree.
  Parallelization: Can parallel Y | Wave 1 | Blocks 3, 5, 6
  References:
  - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h:111`
  - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc:1931`
  - `maho-chromium/browser/ui/webui/maho_test/maho_test_ui.cc:841`
  Acceptance criteria (agent-executable):
  - `git status --short -- maho-chromium/browser/ui/views/sidebar maho-chromium/browser/ui/webui/maho_test maho-chromium/browser/ui/views/sidebar/BUILD.gn > .omo/evidence/task-1-arc-sidebar-folder-height-status.txt`
  - `git diff -- maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_dispatch_unittest.cc maho-chromium/browser/ui/views/sidebar/BUILD.gn > .omo/evidence/task-1-arc-sidebar-folder-height-diff.txt`
  - `grep -n "kFolderRowHeightDp\\|kFolderGlyphContainerSizeDp\\|kFolderGlyphSizeDp\\|kSectionSeparatorHeightDp\\|kRowHeightDp" maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h > .omo/evidence/task-1-arc-sidebar-folder-height-current-tokens.txt`
  QA scenarios:
  - Happy: launch `Maho.app` with `/tmp/maho-folder-height-profile-before` and `chrome://maho-test/?seed=2`, then capture `.omo/artifacts/arc-sidebar-folder-height/before-window.png`.
  - Failure: if the current app cannot launch before changes, record the launch error in `.omo/evidence/task-1-arc-sidebar-folder-height-before-launch.txt` and continue because after-build visual QA remains mandatory.
  Commit: N | evidence only | Files `.omo/evidence/*`, `.omo/artifacts/arc-sidebar-folder-height/*`

- [x] 2. Confirm the existing unit-test seam and folder seed path
  What to do / Must NOT do:
  Re-read the exact current tests before editing. Prefer `maho_sidebar_dnd_dispatch_unittest.cc` for the folder-row preferred-height regression because it already directly constructs `SidebarFolderRowView` in an existing `unit_tests` target. Confirm `chrome://maho-test/?seed=2` still creates `Work`, `Planning`, and `Research` for screenshot QA. Do not add a new test target or GN dependency.
  Parallelization: Can parallel Y | Wave 1 | Blocks 3, 4
  References:
  - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_dispatch_unittest.cc:650`
  - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_contract_test.cc:297`
  - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h:117`
  - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_interactive_test_base.cc:157`
  - `maho-chromium/browser/ui/webui/maho_test/maho_test_ui.cc:261`
  Acceptance criteria (agent-executable):
  - Save a short note at `.omo/evidence/task-2-arc-sidebar-folder-height-test-seam.md` naming `maho_sidebar_dnd_dispatch_unittest.cc` unless a stronger existing exact assertion is found.
  - The note must include the exact direct-construction path for `SidebarFolderRowView` and the target command `chromium/src/out/Default/unit_tests --gtest_filter="SidebarDnDDispatchTest.*Folder*"`.
  QA scenarios:
  - Happy: `maho_sidebar_dnd_dispatch_unittest.cc` can assert folder row preferred height without new GN wiring.
  - Failure: if direct construction cannot produce a stable preferred-height assertion, use `maho_sidebar_contract_test.cc` as fallback and record why the unit seam was rejected.
  Commit: N | evidence only | Files `.omo/evidence/task-2-arc-sidebar-folder-height-test-seam.md`

- [x] 3. Add folder-row density regression and apply the token fix
  What to do / Must NOT do:
  In `maho_sidebar_dnd_dispatch_unittest.cc`, add an assertion that `SidebarFolderRowView::GetPreferredSize().height()` tracks the compact row target. Then update folder tokens in `maho_sidebar_layout_tokens.h`. Keep the fix token-first; edit `SidebarFolderRowView` only if the test or visual measurement proves the existing insets mis-center the content after token changes.
  Parallelization: Can parallel N | Wave 2 | Blocks 4, 5, 6
  References:
  - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h:111`
  - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h:121`
  - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc:1899`
  - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc:1910`
  - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc:1920`
  - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc:1931`
  Required implementation:
  - Change `kFolderRowHeightDp` from `40` to `34`.
  - Change `kFolderGlyphContainerSizeDp` from `28` to `22`.
  - Change `kFolderGlyphSizeDp` from `16` to `14`.
  - Leave `kChevronSizeDp = 12`.
  - Leave `kFolderIndentDp = 14`.
  - Leave `kSectionSeparatorHeightDp = 22` for this todo.
  - Update comments so parity is defined as Maho/Arc compact sidebar density and folder-row equality with the regular tab-row rhythm, not Zen `40px` folder parity.
  Acceptance criteria (agent-executable):
  - The selected test fails or would fail against the old `40 dp` folder height, then passes after the token change.
  - `grep -n "kFolderRowHeightDp\\|kFolderGlyphContainerSizeDp\\|kFolderGlyphSizeDp" maho-chromium/browser/ui/views/sidebar/maho_sidebar_layout_tokens.h` shows `34`, `22`, and `14`.
  - The test assertion uses the named token or `kRowHeightDp` relationship, not a duplicate unexplained magic number.
  - The test does not add glyph/container child-view accessors; glyph sizing is verified by token grep and final screenshot QA.
  QA scenarios:
  - Happy: folder row preferred height equals `sidebar_layout::kRowHeightDp` or the live folder row bounds measure to `34 dp` after layout.
  - Failure: if the view reports a larger live bound because of parent layout, record the parent layout reason and assert the nearest stable contract, then keep screenshot QA as the final arbiter.
  Commit: Y | fix(sidebar): compact folder row density | Files `maho_sidebar_layout_tokens.h`, selected sidebar test file, optionally `maho_sidebar_tab_list_view.cc`

- [x] 4. Audit DnD and browser tests for stale 40 px folder geometry
  What to do / Must NOT do:
  Inspect folder-row tests with hard-coded `SetBoundsRect(..., 40)`. Replace values with `sidebar_layout::kFolderRowHeightDp` and derive pointer coordinates from that height where the test is modeling the real folder-row height. Leave deliberately synthetic 30/40 px hit-zone tests unchanged if their purpose is proportional drop-zone behavior independent of actual row height, and add a short comment only if the distinction would otherwise be unclear.
  Parallelization: Can parallel N | Wave 2 | Blocks 5
  References:
  - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_dispatch_unittest.cc`
  - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_browser_test.cc`
  - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_interactive_test.cc`
  - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_edge_cases_interactive_test.cc`
  Acceptance criteria (agent-executable):
  - `grep -RIn "SetBoundsRect(gfx::Rect(0, 0, 220, 40))" maho-chromium/browser/ui/views/sidebar --include='*.cc'` is reviewed line by line.
  - Save `.omo/evidence/task-4-arc-sidebar-folder-height-dnd-geometry.md` listing each changed or intentionally unchanged folder-row geometry site.
  - No DnD test expectation is weakened or removed.
  - `SidebarDnDDispatchTest.*Folder*` still covers before/into/after drop-zone thresholds after the row height change.
  QA scenarios:
  - Happy: tests that represent actual folder row height now use `kFolderRowHeightDp`; synthetic drop-zone tests retain explicit heights with rationale.
  - Failure: if including layout tokens in a test would create a dependency cycle, leave the value explicit, add a local named constant in the test, and document it.
  Commit: Y | test(sidebar): align folder geometry assumptions | Files relevant sidebar test files

- [x] 5. Build and run targeted verification through the wrapper
  What to do / Must NOT do:
  Build the app and the narrow test target through the canonical wrapper. Do not modify GN files to chase a build failure without explicit user approval. If the build fails in GN regen or later, stop and report the exact failure, preserving all logs.
  Parallelization: Can parallel N | Wave 3 | Blocks 6
  References:
  - `AGENTS.md` Chromium build instructions
  - `maho-chromium/build/scripts/build_maho.py`
  - selected test target from Todo 2
  Acceptance criteria (agent-executable):
  - `python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target chrome | tee .omo/evidence/task-5-arc-sidebar-folder-height-build-chrome.log`
  - `python3 maho-chromium/build/scripts/build_maho.py --skip-rust --ninja-target unit_tests | tee .omo/evidence/task-5-arc-sidebar-folder-height-build-test.log`
  - `chromium/src/out/Default/unit_tests --gtest_filter="SidebarDnDDispatchTest.*Folder*" 2>&1 | tee .omo/evidence/task-5-arc-sidebar-folder-height-test.log`
  QA scenarios:
  - Happy: build exits 0 and the new/updated folder-row assertion plus folder-related DnD tests pass.
  - Failure: if unrelated tests fail, record the exact failing test names and evidence; do not hide or delete them.
  Commit: N | verification only | Files `.omo/evidence/task-5-*`

- [x] 6. Perform seeded screenshot QA and final separator decision
  What to do / Must NOT do:
  Launch the built app with a fresh profile and `chrome://maho-test/?seed=2`, then capture the full window and a sidebar crop containing `Work`, `Planning`, and `Research`. Measure folder row height against adjacent normal rows. If the row itself is fixed but separator spacing still makes the folder block visually too tall, reduce `kSectionSeparatorHeightDp` from `22` to `14`, rebuild the affected target, and repeat the screenshot capture.
  Parallelization: Can parallel N | Wave 3 | Blocks final verification
  References:
  - `maho-chromium/browser/ui/webui/maho_test/maho_test_ui.cc:841`
  - `maho-chromium/browser/ui/webui/maho_test/maho_test_ui.cc:860`
  - `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc:266`
  - `.omo/artifacts/arc-sidebar-look-and-feel/final-qa/arc-vs-maho-sidebar-final-side-by-side.png`
  Acceptance criteria (agent-executable):
  - `.omo/artifacts/arc-sidebar-folder-height/after-window.png` exists.
  - `.omo/artifacts/arc-sidebar-folder-height/after-sidebar-folder-crop.png` exists and visibly contains at least one folder row.
  - `.omo/artifacts/arc-sidebar-folder-height/measurement.md` records observed folder row height, adjacent normal row height, glyph container size, and whether separator was changed.
  - Folder row height is within `1 dp` of normal tab row height in the final measurement.
  - If `kSectionSeparatorHeightDp` is changed, Todo 5 build/test verification is repeated and logs are updated.
  QA scenarios:
  - Happy: folder rows visually align with normal tab density and no icon/text clipping appears.
  - Failure: if seeded folders do not render, capture the page state and logs, then use the existing interactive test seed helper or manual shell event route to create a folder-containing sidebar before screenshotting. If no real app screenshot can be captured, stop and report the visual-QA blocker instead of claiming done.
  Commit: Y if separator changed | fix(sidebar): reduce folder section separator density | Files `maho_sidebar_layout_tokens.h`, evidence/artifacts

## Final Verification Wave
> Runs in parallel where possible. ALL must APPROVE. Surface results and wait for the user's explicit okay before declaring complete.

- [x] F1. Plan compliance audit
  Verify every Must have is satisfied and every Must NOT have is preserved. Evidence: `.omo/evidence/f1-arc-sidebar-folder-height-plan-compliance.md`.

- [x] F2. Code quality review
  Review the final diff for token consistency, no stale comments, no unrelated sidebar restyling, no GN workaround edits, and no accidental test weakening. Evidence: `.omo/evidence/f2-arc-sidebar-folder-height-code-quality.md`.

- [x] F3. Real manual QA
  Inspect final screenshots from `chrome://maho-test/?seed=2`, including full window and folder crop. Confirm folder rows no longer read taller than regular tab rows and that `Work`, `Planning`, or `Research` is visible. Evidence: `.omo/evidence/f3-arc-sidebar-folder-height-visual-qa.md`.

- [x] F4. Scope fidelity
  Confirm the diff is limited to folder density tokens/tests and necessary evidence. Explicitly list any touched files outside the expected scope. Evidence: `.omo/evidence/f4-arc-sidebar-folder-height-scope.md`.

## Commit Strategy
- Keep commits atomic if the executor is asked to commit:
  - `fix(sidebar): compact folder row density`
  - `test(sidebar): align folder row density regression`
  - optional `fix(sidebar): reduce folder section separator density`
- Do not commit `.omo/evidence` or `.omo/artifacts` unless the project convention for this branch requires visual QA artifacts in git.
- Do not amend or squash without explicit user request.

## Success Criteria
- Folder row height is no longer visually taller than normal tab rows in a seeded sidebar with real folders.
- `kFolderRowHeightDp` matches `kRowHeightDp` at `34 dp`.
- Folder glyph and container sizing are compact enough to avoid the oversized-folder appearance without clipping.
- Focused regression coverage exists for the folder-row height contract in an existing sidebar test target.
- Canonical wrapper build succeeds for `chrome` and the selected test target.
- Selected targeted test filter passes.
- Final screenshot artifacts show the real app with `chrome://maho-test/?seed=2` and visible folder rows.
- No GN workaround edits are made.
- No unrelated dirty worktree changes are reverted or overwritten.
