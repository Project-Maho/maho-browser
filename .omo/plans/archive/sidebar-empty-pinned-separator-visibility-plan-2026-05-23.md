# Sidebar Empty Pinned Separator Visibility Plan

## Goal

When the sidebar tab list resolves to an empty pinned section while normal content (live or model tree) still exists, the horizontal separator that sits directly above the inline `+ New Tab` row must remain visible. The pinned section container itself must stay hidden in that same state, exactly as it does today.

The visual outcome the user expects:

1. Pinned tree is empty.
2. Normal tree (or live-strip fallback) is non-empty.
3. The pinned section drop target container is in the view tree but not visible.
4. The separator row is in the view tree and is visible, sitting between the hidden pinned section and the visible `+ New Tab` row.
5. The `+ New Tab` row and the normal section render normally below the separator.

True zero-tab (no pinned tree, no normal tree, no live-strip fallback) must keep its current CTA-only shape. Nothing about that branch changes.

## Target contract

In `MahoSidebarTabListView::RebuildRows()` for the empty-pinned, non-empty-normal case (covering both the model-tree branch and the live-strip fallback branch), the resulting `tab_rows_` view tree must satisfy:

1. `tab_rows_->children().size() == 4`
2. `tab_rows_->children()[0]` is the pinned section drop target container, with `GetVisible() == false`.
3. `tab_rows_->children()[1]` is the pinned separator row, with `GetVisible() == true`.
4. `tab_rows_->children()[2]` is the inline-row button labelled `u"+ New Tab"`, with `GetVisible() == true`.
5. `tab_rows_->children()[3]` is the normal section drop target container, with `GetVisible() == true` and at least one child.

Helper signal of the new contract: `UpdatePinnedDropLaneVisibility(false)` must no longer hide the separator. The separator is owned by a different visibility rule from the pinned section container.

## Explicit non-goals

1. No change to the true zero-tab CTA-only branch in `RebuildRows()`. That branch still constructs only `+ New Tab` and leaves `pinned_section_target_`, `normal_section_target_`, and `pinned_separator_` null.
2. No change to the non-empty-pinned render shape. When `pinned_tree` is non-empty, both pinned section container and separator are visible exactly as today.
3. No change to drop-lane reveal behavior. `pinned_drop_lane_revealed_` still drives the temporary reveal of the pinned section container during DnD. The separator simply does not need that signal to stay visible in the empty-pinned idle state.
4. No change to `RebuildTreeSection`, `SidebarSectionDropTarget`, `SidebarSeparatorRow`, or any drawing code for the separator or section drop targets.
5. No change to favorites grid, library rail, archive view, footer, command palette, or any other non-tab-list sidebar surface.
6. No change to upstream Chromium tab strip behavior, NTP behavior, or anything that controls when the browser actually has zero tabs.

## Current code findings

### Source: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`

`MahoSidebarTabListView::RebuildRows(model, browser)` has three render branches and one helper that all converge on the same separator visibility rule.

1. `has_tree` is true (model has pinned and/or normal tree). The function builds the pinned section drop target via `RebuildTreeSection(...)`, appends a `SidebarSeparatorRow`, computes `pinned_visible = !model.pinned_tree.empty() || pinned_drop_lane_revealed_`, then calls `pinned_section_target_->SetVisible(pinned_visible)` and `pinned_separator_->SetVisible(pinned_visible)`. The `+ New Tab` inline row and the normal section drop target are appended after.
2. `has_tree` is false but a live-strip fallback model produces `fallback_has_tree`. The branch repeats the exact same shape using fallback data, including the same `pinned_visible` formula and the same paired `SetVisible` calls on the section container and separator.
3. `has_tree` is false and the live-strip fallback is also empty or unavailable. This is the true zero-tab branch. It only appends the `+ New Tab` CTA and returns. Pinned section container, separator, and normal section drop target are not constructed. This branch is out of scope for this plan.

`MahoSidebarTabListView::UpdatePinnedDropLaneVisibility(bool visible)` currently flips both the pinned section container and the separator together:

```
if (pinned_section_target_) {
  drop_target->SetMinDropTargetHeight(visible ? kRowHeightDp : 0);
  pinned_section_target_->SetVisible(visible);
}
if (pinned_separator_) {
  pinned_separator_->SetVisible(visible);
}
```

`pinned_drop_lane_revealed_` toggling triggers this helper from `OnEnterPinnedDropLane` / `OnExitPinnedDropLane`. The separator is therefore tied to drop-lane reveal state today, not just to the idle empty-pinned state.

The current rule, summarized in one sentence: separator visibility is the same boolean as pinned section container visibility, both at initial layout (`pinned_visible` formula) and during drop-lane reveal toggles (`UpdatePinnedDropLaneVisibility`).

The new rule: the separator is visible whenever the surrounding layout has any tab content above or below it (i.e. branches 1 and 2 above), independent of whether the pinned section container itself is visible. The pinned section container keeps its existing visibility rule unchanged.

### Source: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_contract_test.cc`

`ExpectEmptyPinnedSectionLayout(views::View* rows)` is the contract helper that locks the empty-pinned, non-empty-normal layout shape. Today it asserts:

1. `rows->children().size() == 4` — pinned section, separator, `+ New Tab`, normal section.
2. `rows->children()[0]->GetVisible() == false` — pinned section hidden.
3. `rows->children()[1]->GetVisible() == false` — separator hidden.
4. `IsInlineRowButton(rows->children()[2], u"+ New Tab")` and visible.
5. `rows->children()[3]->GetVisible() == true` and has children.

Step 3 (separator hidden) is the assertion that contradicts the new target. Steps 1, 2, 4, and 5 already match the new target.

The single caller is `SidebarRowOrderTreeLayoutKeepsEmptyPinnedSectionContainer` (sets `pinned_tree` to empty on a Seed 2 model, calls `tab_list_->Update(...)`, then `ExpectEmptyPinnedSectionLayout(tab_list_->tab_rows_for_testing())`). Updating the helper automatically updates that test.

`ExpectNoTabsRows(views::View* rows)` is the true zero-tab helper. It already locks a one-child `+ New Tab` shape. It is not touched by this plan.

### Source: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h`

`pinned_separator_` is a `raw_ptr<views::View>` member set in both render branches and reset to `nullptr` at the top of `RebuildRows()`. No header change is required for this plan because the new behavior is fully expressible at the use sites in the `.cc` file.

## File-by-file implementation plan

### File 1: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`

Two related edits, all inside `RebuildRows()` and `UpdatePinnedDropLaneVisibility()`. No other functions in this file change.

#### Edit 1.1: Decouple separator visibility from `pinned_visible` in the live-strip fallback render branch.

Location: inside `if (!has_tree)` → `if (live_tabs_exist)` → `if (fallback_has_tree)`, around lines 2372 to 2390.

Current shape:

```
RebuildTreeSection(tab_rows_, fallback_model.pinned_tree, kPinned, browser);
auto separator = std::make_unique<SidebarSeparatorRow>();
pinned_separator_ = tab_rows_->AddChildView(std::move(separator));
const bool pinned_visible = !fallback_model.pinned_tree.empty() ||
                            pinned_drop_lane_revealed_;
auto* pinned_drop_target = static_cast<SidebarSectionDropTarget*>(
    pinned_section_target_);
if (fallback_model.pinned_tree.empty()) {
  pinned_drop_target->SetMinDropTargetHeight(kRowHeightDp);
}
pinned_section_target_->SetVisible(pinned_visible);
pinned_separator_->SetVisible(pinned_visible);
```

Target shape:

1. Keep the `RebuildTreeSection` call, the separator construction, and the `pinned_visible` computation as-is.
2. Keep `pinned_section_target_->SetVisible(pinned_visible)` exactly as today. The pinned section container visibility rule does not change.
3. Replace `pinned_separator_->SetVisible(pinned_visible)` with `pinned_separator_->SetVisible(true)`. The separator is always visible in this branch because by construction the live-strip fallback has at least one tab, so there is always content below the separator.
4. Do not touch the `SetMinDropTargetHeight(kRowHeightDp)` call. Drop-target height is independent of separator visibility.

#### Edit 1.2: Decouple separator visibility from `pinned_visible` in the model-tree render branch.

Location: the `has_tree == true` branch, around lines 2424 to 2436.

Current shape:

```
RebuildTreeSection(tab_rows_, model.pinned_tree, kPinned, browser);
auto separator = std::make_unique<SidebarSeparatorRow>();
pinned_separator_ = tab_rows_->AddChildView(std::move(separator));
const bool pinned_visible = !model.pinned_tree.empty() ||
                            pinned_drop_lane_revealed_;
auto* pinned_drop_target =
    static_cast<SidebarSectionDropTarget*>(pinned_section_target_);
if (model.pinned_tree.empty()) {
  pinned_drop_target->SetMinDropTargetHeight(kRowHeightDp);
}
pinned_section_target_->SetVisible(pinned_visible);
pinned_separator_->SetVisible(pinned_visible);
```

Target shape:

1. Same as Edit 1.1: keep `pinned_section_target_->SetVisible(pinned_visible)` unchanged.
2. Replace `pinned_separator_->SetVisible(pinned_visible)` with `pinned_separator_->SetVisible(true)`. By construction `has_tree` is true, so at least one of `pinned_tree` or `normal_tree` is non-empty, and the `+ New Tab` row is always inserted between the separator and the normal section. The separator therefore always has at least one neighbor below it and one above it (or above it via the hidden pinned section container which still occupies a slot in `children()`).

#### Edit 1.3: Stop forcing the separator hidden from `UpdatePinnedDropLaneVisibility`.

Location: `MahoSidebarTabListView::UpdatePinnedDropLaneVisibility(bool visible)`, around lines 2341 to 2350.

Current shape:

```
void MahoSidebarTabListView::UpdatePinnedDropLaneVisibility(bool visible) {
  if (pinned_section_target_) {
    auto* drop_target = static_cast<SidebarSectionDropTarget*>(pinned_section_target_);
    drop_target->SetMinDropTargetHeight(visible ? kRowHeightDp : 0);
    pinned_section_target_->SetVisible(visible);
  }
  if (pinned_separator_) {
    pinned_separator_->SetVisible(visible);
  }
}
```

Target shape:

1. Keep the `pinned_section_target_` block exactly as today. Drop-lane reveal still toggles section container visibility and minimum drop-target height.
2. Either remove the `pinned_separator_` `SetVisible` block entirely, or replace it with `pinned_separator_->SetVisible(true)`. The separator must not be flipped to hidden by drop-lane state. Choose the explicit `SetVisible(true)` form so the function still defensively asserts the desired state in case some other code path leaves the separator hidden. A short comment of the form `// Separator stays visible regardless of pinned-section drop-lane state.` makes the intent obvious to future readers.
3. Do not introduce any new field, helper, or signal. The function keeps its signature and call sites.

#### Constraints for this file

1. Do not touch the true zero-tab branch (lines 2416 to 2421 in current code). That branch still constructs only `+ New Tab` and returns.
2. Do not touch `RebuildTreeSection`, `SidebarSectionDropTarget`, `SidebarSeparatorRow`, or any layout / synthesize-mouse-move follow-up code.
3. Do not introduce a new `separator_visible` field or a new helper. The new rule is local to the three call sites above.
4. Do not change the order of children in `tab_rows_`. Pinned section, separator, `+ New Tab`, normal section, in that order.
5. Do not change `pinned_drop_lane_revealed_`, `OnEnterPinnedDropLane`, or `OnExitPinnedDropLane`. They keep flipping section container visibility through `UpdatePinnedDropLaneVisibility` exactly as today; only the separator side of that helper changes.

### File 2: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_contract_test.cc`

One edit, in the helper `ExpectEmptyPinnedSectionLayout(views::View* rows)`.

#### Edit 2.1: Flip the separator visibility expectation to `true`.

Location: lines 47 to 64 in current code.

Current shape:

```
EXPECT_FALSE(rows->children()[1]->GetVisible())
    << "Pinned separator should be hidden when empty";
```

Target shape:

```
EXPECT_TRUE(rows->children()[1]->GetVisible())
    << "Pinned separator should remain visible above + New Tab even when the pinned section is empty";
```

Other assertions in the helper are kept verbatim:

1. `ASSERT_EQ(4u, rows->children().size())` and its message.
2. `EXPECT_FALSE(rows->children()[0]->GetVisible())` (pinned section container still hidden).
3. `EXPECT_TRUE(IsInlineRowButton(rows->children()[2], u"+ New Tab"))` and the visibility check on `[2]`.
4. `EXPECT_TRUE(rows->children()[3]->GetVisible())` and the non-empty-children check on `[3]`.

#### Constraints for this file

1. Do not change the helper signature. The single caller `SidebarRowOrderTreeLayoutKeepsEmptyPinnedSectionContainer` picks up the new contract automatically.
2. Do not touch `ExpectNoTabsRows` or its caller `SidebarRowOrderNoTabsRendersOnlyNewTabCta`. True zero-tab CTA-only contract is unchanged.
3. Do not touch any other test in this file. Other tests (`SeededFavoritesMatchSnapshot`, `SeededFoldersMatchSnapshot`, `PinnedSectionContract`, `FolderSupportContract`, the active-space-id refresh tests, footer tests, etc.) make no assertion about separator visibility and will not be affected.
4. Do not add new test cases in this pass. Reuse the existing caller as the lock point.
5. Do not change imports, fixtures, profile seeding, or `IsInlineRowButton`.

### Files intentionally not modified

The following files were inspected and confirmed to be out of scope. They must not be touched in this pass.

1. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h`. The new behavior is fully expressible at the existing call sites; no new field or method is needed. `pinned_separator_` keeps its existing type and lifetime.
2. The true zero-tab branch in `RebuildRows()` (the path that constructs only `+ New Tab` and returns). That branch never constructs a separator and is unaffected by the new rule.
3. `OnEnterPinnedDropLane` / `OnExitPinnedDropLane` and the `pinned_drop_lane_revealed_` field. They keep flipping pinned section container visibility through `UpdatePinnedDropLaneVisibility` exactly as today; the separator side of that helper is the only thing that changes.
4. `SidebarSeparatorRow` and `SidebarSectionDropTarget` view classes. No painting, sizing, or interaction change is needed.
5. `RebuildTreeSection`. The pinned and normal section construction is unchanged.
6. `ExpectNoTabsRows` and `SidebarRowOrderNoTabsRendersOnlyNewTabCta`. True zero-tab CTA-only behavior is preserved without any test edit.
7. Other sidebar test files (DnD tests, folder tests, search tests, footer tests, archive tests, etc.). None of them inspect separator visibility in the empty-pinned case based on a grep of `pinned_separator` and `ExpectEmptyPinnedSectionLayout`. If a later inspection finds a test that does assert the old behavior, this list is the place to revisit; do not add such tests now.
8. WebUI sidebar resources, mojom IDL, and Mojo handlers. The empty-pinned separator is a Views-only layout concern.

## Verification plan

### Diagnostics

1. Run `lsp_diagnostics` on `maho_sidebar_tab_list_view.cc` after the edit. Expect zero new errors.
2. Run `lsp_diagnostics` on `maho_sidebar_contract_test.cc` after the edit. Expect zero new errors.

### Unit / interactive tests

1. Run `SidebarRowOrderTreeLayoutKeepsEmptyPinnedSectionContainer`. After the production edit it must pass under the new `ExpectEmptyPinnedSectionLayout` contract.
2. Run `SidebarRowOrderNoTabsRendersOnlyNewTabCta`. It must still pass with no behavioral change.
3. Run the surrounding `MahoSidebarContractTest` suite to confirm seeded-favorites, seeded-folders, pinned-section, folder-support, and footer-height tests are unaffected.
4. If there is a sidebar DnD interactive test that exercises `OnEnterPinnedDropLane` and `OnExitPinnedDropLane`, run it and confirm:
   - Entering the drop lane still makes the pinned section container visible.
   - Exiting the drop lane still hides the pinned section container.
   - The separator is visible throughout, both before, during, and after the drop-lane reveal.

### Manual verification

1. Launch the browser with a profile that has at least one normal tab and zero pinned tabs (or pin nothing on a fresh space).
   - Confirm the separator is visible directly above the `+ New Tab` row.
   - Confirm there is no visible pinned section container above the separator (no empty rectangle, no drop-target outline at idle).
   - Confirm the normal section renders normally below `+ New Tab`.
2. Pin a tab. The pinned section container becomes visible, the separator stays visible, the rest of the layout is unchanged.
3. Unpin all pinned tabs. The pinned section container hides again, the separator stays visible.
4. Drag a normal tab toward the pinned area to trigger the drop-lane reveal. The pinned section container appears with its drop-target height, the separator stays visible. End the drag. The pinned section container hides, the separator stays visible.
5. Reach the true zero-tab state (close every tab, no live-strip fallback). Confirm only `+ New Tab` is visible, with no separator and no pinned/normal section. This is unchanged from today.
6. Toggle between empty pinned and non-empty pinned several times rapidly. Confirm there is no flicker of the separator. The separator never flashes hidden.

## Risks

1. The separator was historically tied to pinned section visibility. Some future code path may still expect `UpdatePinnedDropLaneVisibility(false)` to imply a hidden separator. Mitigation: search for inline assertions on `pinned_separator_->GetVisible()` and on `children()[1]->GetVisible()` in sidebar test files before landing. The contract helper is the canonical lock point; other inline assertions should be updated only if they directly contradict the new rule.
2. If a future render branch decides to construct the separator without a `+ New Tab` row or normal section below it, the new always-visible rule could leave a stranded line. Today no such branch exists. The true zero-tab branch never constructs a separator and remains untouched. Mitigation: keep the separator construction adjacent to the `+ New Tab` insertion in `RebuildRows()` so reviewers can see the invariant at a glance.
3. The drop-lane reveal animation and the empty-pinned idle state share the same code path through `UpdatePinnedDropLaneVisibility`. Splitting the separator off from that helper means the helper now does less. A future refactor that removes the now near-no-op separator block from `UpdatePinnedDropLaneVisibility` should be careful not to remove the comment that explains why the separator stays visible.
4. The contract helper change is a single-line flip. Reviewers may miss the wider implication. Mitigation: the new failure message ("Pinned separator should remain visible above + New Tab even when the pinned section is empty") is the most direct description of the new rule and surfaces the intent in test output.

## Suggested implementation order

1. Update `ExpectEmptyPinnedSectionLayout` in `maho_sidebar_contract_test.cc` first. The existing contract test now fails against the current production code, which proves the contract is being changed and locks the desired shape.
2. Apply Edit 1.1 (live-strip fallback branch) and Edit 1.2 (model-tree branch) in `maho_sidebar_tab_list_view.cc`. The previously failing contract test now passes for both render branches that go through the empty-pinned shape.
3. Apply Edit 1.3 in `UpdatePinnedDropLaneVisibility` so drop-lane reveal toggles do not flip the separator hidden. Re-run any DnD interactive test that exists for the pinned drop lane.
4. Run `lsp_diagnostics` on both changed files.
5. Run the `MahoSidebarContractTest` suite.
6. Walk the manual verification steps, including the empty-pinned, pinned-then-unpinned, drop-lane-reveal, and true zero-tab transitions.

## Done definition

Work is done only when all of the following are true:

1. `RebuildRows()` always sets the separator visible in the model-tree branch and the live-strip fallback branch, independent of `pinned_visible`.
2. `UpdatePinnedDropLaneVisibility(visible)` no longer flips the separator hidden when `visible == false`.
3. `pinned_section_target_` keeps its existing visibility rule (driven by `pinned_visible` at idle and by the `visible` argument during drop-lane reveal).
4. `ExpectEmptyPinnedSectionLayout` asserts the separator is visible in the empty-pinned, non-empty-normal layout. The single caller `SidebarRowOrderTreeLayoutKeepsEmptyPinnedSectionContainer` passes against the new contract without further test edits.
5. `ExpectNoTabsRows` and `SidebarRowOrderNoTabsRendersOnlyNewTabCta` are unchanged and still pass. True zero-tab CTA-only behavior is byte-for-byte preserved.
6. Diagnostics on both changed files are clean.
7. Manual verification confirms the separator stays visible across empty-pinned idle, drop-lane reveal, pin and unpin transitions, and that true zero-tab still renders only `+ New Tab` with no separator.
8. No file outside `maho_sidebar_tab_list_view.cc` and `maho_sidebar_contract_test.cc` is touched in this pass.
