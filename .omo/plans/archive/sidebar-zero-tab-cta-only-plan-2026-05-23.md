# Sidebar Zero-Tab CTA-Only Plan

## Goal

When the sidebar tab list resolves to a true zero-tab state (no model tree, no live-strip fallback tree), the sidebar must render only the `+ New Tab` inline row.

Acceptance standard for true zero-tab:

1. `+ New Tab` is the single visible row in `tab_rows_`.
2. The pinned section drop target is not constructed.
3. The pinned/normal separator is not constructed.
4. The normal section drop target is not constructed.
5. No `No tabs open` placeholder, label, or stub view is rendered.

## Target contract

In `MahoSidebarTabListView::RebuildRows()`, the true zero-tab branch produces a `tab_rows_` view tree with exactly one child:

1. `tab_rows_->children().size() == 1`
2. `tab_rows_->children()[0]` is the inline-row button labelled `u"+ New Tab"`
3. `tab_rows_->children()[0]->GetVisible() == true`
4. `pinned_section_target_ == nullptr`
5. `normal_section_target_ == nullptr`
6. `pinned_separator_ == nullptr`
7. `new_tab_button_` points at the single CTA child

## Explicit non-goals

1. No change to the `has_tree` render path that handles non-empty pinned or normal trees.
2. No change to the `fallback_has_tree` render path that handles the live-strip fallback when the model tree is empty but live tabs exist.
3. No change to `RebuildTreeSection`, drop planner code, separator visuals, section drop-target visuals, or hover behavior.
4. No change to upstream Chromium startup, zero-tab persistence, NTP behavior, or anything that controls when the browser actually has zero tabs. Those files are out of scope for this pass.
5. No change to favorites grid, library rail, archive view, or any non-tab-list sidebar surface.
6. No new pinned-only or normal-only branching introduced as a side effect of the CTA-only branch.

## Current code findings

### Source: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`

`MahoSidebarTabListView::RebuildRows(model, browser)` currently has three branches:

1. `has_tree` is true (model has pinned and/or normal tree). Renders pinned section + separator + `+ New Tab` + normal section.
2. `has_tree` is false but a live-strip fallback model produces `fallback_has_tree`. Renders the same shape using fallback data.
3. `has_tree` is false and the live-strip fallback is also empty or unavailable. Today this branch returns early after `RemoveAllChildViews()`, leaving `tab_rows_` with zero children.

That third branch is the true zero-tab case. The user-visible result today is an empty list with no CTA, no separator, no sections.

### Target shape for the third branch

Keep `RemoveAllChildViews()` and the nulling of `pinned_section_target_` / `normal_section_target_` / `pinned_separator_`. Then, instead of returning silently, append a single inline row:

- label: `u"+ New Tab"`
- callback: existing `OpenNewTab` weak-bound callback
- `active=false`, `subtle=true` (matches existing CTA call sites)
- assign the result to `new_tab_button_`

No call to `RebuildTreeSection`. No separator. No section drop targets. No placeholder.

### Source: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_contract_test.cc`

`ExpectNoTabsRows(rows)` currently asserts `rows->children().size() == 0u`, with the message `"No-tabs layout should render completely blank with zero children"`. That contract directly contradicts the new target.

The test `SidebarRowOrderNoTabsKeepsPlaceholderWithoutSeparator` is the only caller of `ExpectNoTabsRows` and is the lock point for the zero-tab shape.

## File-by-file implementation plan

### File 1: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`

Function: `MahoSidebarTabListView::RebuildRows(const MahoSidebarTabListModel& model, Browser* browser)`.

Changes:

1. Keep the existing prelude:
   - `HandleTabRowHoverEnd();`
   - `tab_rows_->RemoveAllChildViews();`
   - reset `pinned_section_target_`, `normal_section_target_`, `pinned_separator_` to `nullptr`.
2. Keep the `has_tree` computation and the `has_tree == true` render branch unchanged.
3. Inside the `if (!has_tree)` block, keep the live-strip fallback computation and the `fallback_has_tree == true` render branch unchanged.
4. Replace the silent early return at the end of the `if (!has_tree)` block with a CTA-only true zero-tab branch:
   - call `AddInlineRow(tab_rows_, u"+ New Tab", base::BindRepeating(&MahoSidebarTabListView::OpenNewTab, weak_factory_.GetWeakPtr()), /*active=*/false, /*subtle=*/true)` and assign the result to `new_tab_button_`.
   - do not construct any separator.
   - do not construct any pinned/normal section drop target.
   - do not insert any placeholder, label, or stub.
5. Preserve the existing widget layout/synthesize-mouse-move follow-up only for the `has_tree` and `fallback_has_tree` branches that already use it. The CTA-only branch does not need that follow-up because there is no tree section to lay out, but if a single CTA child still benefits from it, mirror the existing pattern verbatim. Choose the smaller change first: no synthesize-mouse-move work in the CTA-only branch unless verification shows it is required.

Constraints for this file:

1. Do not touch `RebuildTreeSection`.
2. Do not touch the `has_tree` branch or the `fallback_has_tree` branch.
3. Do not introduce new fields, new helpers, or new sections.
4. Do not pin-special-case the CTA-only branch; it uses the same `AddInlineRow` call shape as the other branches.

### File 2: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_contract_test.cc`

Function: `ExpectNoTabsRows(views::View* rows)`.

Changes:

1. Replace the existing zero-children assertion with a one-child CTA contract:
   - `ASSERT_TRUE(rows);`
   - `ASSERT_EQ(1u, rows->children().size())` with a message stating that the true zero-tab layout must render only the `+ New Tab` CTA.
   - `EXPECT_TRUE(IsInlineRowButton(rows->children()[0], u"+ New Tab"))` to lock the label.
   - `EXPECT_TRUE(rows->children()[0]->GetVisible())` to lock visibility.
2. Keep the helper signature `void ExpectNoTabsRows(views::View* rows)` so existing call sites do not change.
3. Do not modify any other helper. Do not modify any other test case body. The single existing caller (`SidebarRowOrderNoTabsKeepsPlaceholderWithoutSeparator`) automatically picks up the new contract.

Constraints for this file:

1. Do not add new test cases in this pass.
2. Do not change the test fixture, profile seeding, or imports beyond what the new assertions require.
3. Do not assert anything about pinned section, normal section, or separator presence beyond what falls out of `children().size() == 1`.

## Verification plan

### Diagnostics

1. Run `lsp_diagnostics` on `maho_sidebar_tab_list_view.cc` after the edit. Expect zero new errors.
2. Run `lsp_diagnostics` on `maho_sidebar_contract_test.cc` after the edit. Expect zero new errors.

### Unit / interactive tests

1. Run the contract test that exercises the no-tabs layout, currently named `SidebarRowOrderNoTabsKeepsPlaceholderWithoutSeparator`, and confirm it now passes against the new `ExpectNoTabsRows` contract.
2. Run the surrounding sidebar contract suite to confirm the `has_tree` row-order tests are unaffected.

### Manual verification

1. Launch the browser into a state where the sidebar model resolves to true zero tabs (no live tabs, empty pinned tree, empty normal tree). Confirm visually:
   - `+ New Tab` is rendered.
   - No separator line is visible above or below it.
   - No pinned section block is visible.
   - No normal section block is visible.
   - No `No tabs open` text or placeholder is present.
2. Click `+ New Tab` from the true zero-tab state and confirm `OpenNewTab` fires and the sidebar transitions correctly into the `has_tree` or `fallback_has_tree` shape on the next rebuild.
3. Open and close tabs to walk through:
   - true zero-tab → one normal tab (should switch to `has_tree` render path)
   - one normal tab → true zero-tab (should switch back to CTA-only render path)
   Confirm no flicker of separator, sections, or placeholder during the transitions.

## Risks

1. The true zero-tab state may be hard to reach in normal browser usage because Chromium tends to keep at least one tab and the live-strip fallback tends to fill in. The CTA-only branch will mostly fire in tests and edge cases. That is acceptable for this scope. Mitigation: rely on the contract test to lock the shape rather than relying on manual reproduction alone.
2. The synthesize-mouse-move follow-up exists in the other two branches for hover correctness. Skipping it in the CTA-only branch is intentional and minimal, but if hover state on `+ New Tab` looks stale right after entering true zero-tab, mirror the existing follow-up pattern verbatim into this branch.
3. Any future code that assumes `tab_rows_->children().empty()` implies true zero-tab will break. The new contract makes a single CTA child the canonical signal. Search for empty-children assumptions before landing if reviewers flag this.
4. Tests that previously relied on `ExpectNoTabsRows` asserting zero children will now assert one child. The single existing caller is updated automatically by editing the helper, but reviewers should still scan for any inline assertion of `children().size() == 0u` on `tab_rows_`.

## Suggested implementation order

1. Update `ExpectNoTabsRows` in `maho_sidebar_contract_test.cc` to the one-child CTA contract first. This makes the existing no-tabs test fail against the current production code, locking the desired shape.
2. Add the CTA-only true zero-tab branch to `RebuildRows()` in `maho_sidebar_tab_list_view.cc`. The previously failing test now passes.
3. Run diagnostics on both changed files.
4. Run the sidebar contract test suite.
5. Walk the manual verification steps.

## Done definition

Work is done only when all of the following are true:

1. `RebuildRows()` has a CTA-only branch for the true zero-tab case that constructs only `+ New Tab` and leaves separator and section pointers null.
2. The `has_tree` and `fallback_has_tree` render paths are byte-for-byte unchanged in behavior.
3. `ExpectNoTabsRows()` enforces the one-child `+ New Tab` contract, including label and visibility.
4. The existing no-tabs contract test passes under the new shape with no other test edits required.
5. Diagnostics on both changed files are clean.
6. Manual verification confirms `+ New Tab` is the only visible row in true zero-tab and that transitions in and out of true zero-tab do not show stray separators, sections, or placeholders.
7. No file outside `maho_sidebar_tab_list_view.cc` and `maho_sidebar_contract_test.cc` is touched in this pass.
