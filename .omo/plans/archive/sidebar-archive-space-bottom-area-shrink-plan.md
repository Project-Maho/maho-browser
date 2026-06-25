# Sidebar bottom area shrink after Archive or Create Space, implementation plan

## Objective
Fix the bug where the sidebar bottom area shrinks or disappears after entering Archive or Create Space, then returning to Tabs. The plan assumes nothing is proven yet, but it starts from the strongest code backed suspect: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`, especially `MahoSidebarView::SetBodyMode(...)`, which hides and shows `top_bar_view_`, `footer_view_`, `tabs_surface_`, `library_container_`, and `archive_view_` without an explicit relayout or preferred size refresh when returning to `kTabs`.

## Current hypothesis ranking
1. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`, `MahoSidebarView::SetBodyMode(...)` fails to trigger the layout refresh needed after mode changes, so the footer or lower body area stays collapsed when returning to `kTabs`.
2. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_container_view.cc`, especially `MahoSidebarContainerView::ApplyPanelState()`, `OnSidebarWidthChanged()`, and `Layout(...)`, may preserve an invalid height or width state that only shows up after a mode switch.
3. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.cc`, especially footer visibility and control rebuilding paths, may be reacting correctly to visibility changes but not to the parent relayout sequence.
4. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_prefs.cc`, especially `ClampSidebarWidthForLayout(...)` and `GetSidebarWidthForLayout(...)`, may be preserving a width that exposes a layout bug, but this is less likely to be the direct cause.
5. `maho-chromium/browser/resources/maho_sidebar/app.ts` and `maho-chromium/browser/resources/maho_sidebar/sidebar.css` may contribute if the bug is actually coming from WebUI state or CSS collapse logic, but they are lower priority because the reported symptom matches native Views visibility and layout state more closely.

## Parallel workstreams
1. `SidebarView layout and mode switch track`
   1. Inspect `MahoSidebarView::SetBodyMode(...)`, `ExitArchiveModeToTabs()`, `HandleCreateSpaceBack()`, and any helper that restores focus or visibility.
   2. Confirm whether the switch back to `kTabs` should explicitly call `InvalidateLayout()`, `PreferredSizeChanged()`, `DeprecatedLayoutImmediately()`, or an equivalent refresh on `this`, `body_host_`, or the parent container.
   3. Define the smallest change that makes the footer and body area recalculate without affecting archive or create space behavior.

2. `Container and preference coupling track`
   1. Inspect `MahoSidebarContainerView::ApplyPanelState()`, `OnSidebarWidthChanged()`, `ApplyPreferredWidthFromPrefs()`, and `UpdateResizeHandleBounds()` for any stale size state that could be surfaced by the mode switch.
   2. Verify whether panel expansion, auto hide, or resize handle visibility can suppress a relayout after returning from Archive or Create Space.
   3. Rule out preference writes as a hidden trigger before changing any width related logic.

3. `Footer and bottom area track`
   1. Inspect `MahoSidebarFooterView::Update(...)`, `UpdateFooterChromeAppearance()`, `OpenSpaceCreationSurface()`, and `CloseActionPopover()` for any side effects that could zero out or hide the footer subtree.
   2. Confirm whether the footer only looks collapsed because its parent did not relayout after visibility toggles, versus the footer itself losing preferred size.
   3. If needed, add a narrow footer state assertion in the regression test to distinguish true footer shrinkage from parent clipping.

4. `WebUI state and styling track`
   1. Inspect `maho-chromium/browser/resources/maho_sidebar/app.ts` for any `collapsed` state changes, DOM replacement, or mode transition side effects that could affect the rendered lower area.
   2. Inspect `maho-chromium/browser/resources/maho_sidebar/sidebar.css` for any rule that could hide, shrink, or clip the lower region when toggling between archive, create space, and tabs states.
   3. Only keep this track active if the native Views layout path does not explain the bug.

5. `Regression coverage track`
   1. Extend the existing interactive coverage in `maho-chromium/browser/ui/views/sidebar/maho_sidebar_navigation_interactive_test.cc` and `maho-chromium/browser/ui/views/sidebar/maho_sidebar_spaces_interactive_test.cc`.
   2. Add or extend a test that reproduces the exact sequence, enter Archive or Create Space, return to Tabs, then assert the footer and lower body are still visible and nonzero in size.
   3. Keep the test focused on the symptom, not the internal fix.

## Synchronization points
1. After the two code inspection tracks finish, compare findings before any edit is made.
2. If the layout bug is confirmed in `MahoSidebarView::SetBodyMode(...)`, stop deeper investigation and move to the minimal fix phase.
3. If the top hypothesis is disproven, activate the container and footer tracks in full, then recheck WebUI only if both native tracks fail to explain the shrink.
4. Before landing any change, align on one concrete reproduction test and one concrete visibility or layout assertion that proves the bug is fixed.

## Decision rules
1. If returning to `kTabs` does not force a fresh layout or preferred size recompute, the fix should start there.
2. If adding a relayout at the `MahoSidebarView` level fixes the bug, do not touch footer, prefs, or WebUI code unless the regression test shows a remaining defect.
3. If the footer still disappears after a sidebar relayout fix, investigate the container and footer tracks before widening the change.
4. If the issue only reproduces when panel expansion or auto hide state changes, treat container preference handling as the likely cause and keep the fix local to `maho_sidebar_container_view.cc`.
5. If the bug is reproducible in WebUI only, prefer the smallest DOM or CSS adjustment and do not mix it with native layout changes.

## Minimal fix policy
1. Change the fewest files possible.
2. Prefer a single layout refresh or preferred size refresh in `maho_sidebar_view.cc` over a broader refactor.
3. Do not change sidebar architecture, mode enums, or preference schema unless the investigation proves they are the real cause.
4. If a broader fix is needed, stop at the first file that definitively resolves the defect and back out any speculative edits.
5. Keep any extra instrumentation out of the final change unless the test needs it.

## Regression test strategy
1. Reuse the interactive sidebar harness under `maho-chromium/browser/ui/views/sidebar/`.
2. Add a focused regression that performs the user path, open Archive, return to Tabs, then verify the footer and bottom area still render with nonzero bounds.
3. Add a sibling path for Create Space if the same bug can be reproduced there, because the issue statement covers both flows.
4. Prefer assertions on visible state, bounds, and preferred size over timing based waits.
5. If the bug depends on a restored focus path, include an assertion that the focus restore does not suppress layout.

## Validation matrix
1. Archive to Tabs path, footer remains visible, body returns to normal tabs surface, footer bounds stay nonzero.
2. Create Space to Tabs path, footer remains visible, tabs surface returns, bottom area stays expanded.
3. Archive back action path, `ExitArchiveModeToTabs()` does not leave the body host clipped.
4. Resize or panel state path, `MahoSidebarContainerView` still reports the expected width and visible resize handle state after the mode switch.
5. WebUI fallback path, if native code is not the root cause, `app.ts` and `sidebar.css` do not leave a collapsed shell behind after the mode switch.

## Deliverables
1. A minimal code fix if the bug is reproduced and explained by the leading hypothesis.
2. One regression test covering Archive return to Tabs, and one for Create Space return to Tabs if needed.
3. A short note in the implementation summary describing which hypothesis won and which suspects were ruled out.

## Definition of done
1. The bug is reproducible before the fix and no longer reproducible after the fix.
2. The footer and lower sidebar area remain visible and nonzero after returning to Tabs from Archive or Create Space.
3. The chosen regression test passes reliably.
4. No unrelated sidebar behavior changes were introduced.
5. The fix stays minimal and only touches files justified by the investigation.

## Recommended execution order
1. Inspect `MahoSidebarView::SetBodyMode(...)` and decide whether a local relayout or preferred size refresh solves the issue.
2. In parallel, verify container and footer state handling only if the first inspection does not fully explain the bug.
3. Add the regression test that matches the reproduced path.
4. Validate the smallest fix against the symptom and stop once the definition of done is satisfied.
