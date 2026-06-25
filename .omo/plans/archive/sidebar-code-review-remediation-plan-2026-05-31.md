# Sidebar Code Review Findings Remediation Plan

## Goal

Address the FAIL verdict from the 5-agent code review of the sidebar sticky-header + always-scrollable + footer-fix work. The review identified 4 MAJOR code quality findings, 1 MEDIUM security finding, and several MINOR/NITPICK items. This plan fixes all blocking items (P0+P1) and selected non-blocking items (P3) to restore review PASS. Test additions (P2) are deferred to a separate follow-up plan due to Chromium test infrastructure complexity, with this plan including a placeholder TODO comment for tracking.

The user's expected outcome:

1. `SidebarSpaceHeaderRow` integrates with the Chromium Views metadata system, matching the convention of every other anonymous Views subclass in `maho_sidebar_tab_list_view.cc`.
2. The `static_cast` from `views::View*` to `SidebarSpaceHeaderRow*` is replaced with a metadata-aware `views::AsViewClass<>` lookup, eliminating type confusion risk.
3. `MahoSidebarStateAdapter` no longer calls `GetCoreFooterSpaceState()` twice per refresh cycle. The result is cached for the duration of one update pass.
4. Out-of-bounds read potential in `BuildTabListModel` is guarded by checking both `space_names.size()` AND `space_icons.size()`.
5. Selected MINOR items are addressed: file-scope constants for auto-scroll magic numbers, `[[maybe_unused]]` annotations on unused parameters, alphabetical include order.
6. Tests for `SidebarSpaceHeaderRow` drop semantics and `SidebarAutoScroller` edge math are tracked via a TODO placeholder pointing to a future follow-up plan.

## Target contract

After this plan is implemented, the following invariants must hold simultaneously:

### Code quality contract — `SidebarSpaceHeaderRow`

1. The class declares `METADATA_HEADER(SidebarSpaceHeaderRow, views::View)` immediately after the `class SidebarSpaceHeaderRow : public views::View {` opening brace, matching the pattern used by `SidebarSeparatorRow`, `SidebarInsertionLaneView`, `SidebarTabRowView`, `SidebarFolderRowView`, and `MahoSidebarTabListView` in the same file.
2. The class has `BEGIN_METADATA(SidebarSpaceHeaderRow)` ... `END_METADATA` block placed immediately after the class definition closes, inside the same anonymous namespace.
3. `UpdateSidebarSpaceHeaderRow()` uses `views::AsViewClass<SidebarSpaceHeaderRow>(row)` and CHECKs the result is non-null when `row` itself is non-null. The static_cast is gone.

### Adapter caching contract — `MahoSidebarStateAdapter`

4. `GetCoreFooterSpaceState()` is called at most once per `BuildViewStateModel(browser)` invocation. The cached result is reused by both `BuildTabListModel()` and `BuildFooterModel()`.
5. Cache lifetime is exactly one `BuildViewStateModel()` pass. It is reset (or recreated) at the start of each new pass to guarantee freshness.
6. The cache implementation does not change the public API of `MahoSidebarStateAdapter`. Callers continue to call `BuildViewStateModel()` exactly as today.

### Bounds safety contract — `BuildTabListModel`

7. The conditional that guards `space_state.space_icons[index]` and `space_state.space_names[index]` checks both `index < space_state.space_names.size()` AND `index < space_state.space_icons.size()`. Both conditions must be true before either subscript runs.

### Code style contract

8. `kAutoScrollEdgeDp` and `kAutoScrollStepDp` and `kAutoScrollHz` are file-scope `constexpr int` constants near the other `k*Dp` constants in the anonymous namespace at the top of `maho_sidebar_tab_list_view.cc`. They are no longer defined inside `SidebarAutoScroller::OnDragOver` or `ScrollTick`.
9. Unused `event` parameters in `SidebarSpaceHeaderRow::OnDragUpdated` are annotated `[[maybe_unused]]` or named `/*event*/`.
10. New `#include` lines in both `maho_sidebar_tab_list_view.cc` and `maho_sidebar_view.cc` are inserted into their alphabetically appropriate position within the existing block, not appended at the end.

### Test placeholder contract

11. A `// TODO(maho-sidebar-tests):` comment in `maho_sidebar_dnd_dispatch_unittest.cc` (or whichever existing test file is most appropriate) cites the future follow-up plan filename. No new test files are created.

### Preserved invariants — must not regress

12. All 22 contract invariants from the prior two plans (`sidebar-sticky-space-header-and-tab-scroll-plan-2026-05-30.md` and `sidebar-footer-regression-and-always-scrollable-fix-plan-2026-05-31.md`) continue to hold.
13. Contract test at `maho_sidebar_dnd_dispatch_unittest.cc:2649-2663` continues to pass (`tab_rows_->parent() == &list`).
14. No new files. No `BUILD.gn` changes.

## Explicit non-goals

1. No removal of `MahoSidebarTabListModel.active_space_icon` and `active_space_name` fields. The code review flagged these as duplicating `MahoSidebarFooterModel.space_icons[active_space_index]`. However, removing them would require either (a) `MahoSidebarView::UpdateTabList()` to access the footer model (architectural cross-coupling between two model push paths), or (b) introducing a shared `ActiveSpaceDisplay` struct and refactoring both models. Either is a multi-file refactor outside the scope of a code-review remediation patch. The duplication is documented and accepted as a known denormalization. The FFI duplication concern is fully addressed by Edits 4-6 in this plan.
2. No actual unit or contract tests added in this round. Test infrastructure for Chromium native Views requires non-trivial setup. A `TODO` comment placeholder is left to track the follow-up.
3. No change to the auto-scroll forwarding architecture (free functions vs. class methods). The Code Quality reviewer suggested making the public methods `private` with `friend` declarations. This trades one form of complexity (exposed surface) for another (friend declarations) and is a stylistic preference, not a correctness issue. Not in scope.
4. No change to `ForwardDragToAutoScroller`'s parent-walk implementation. The reviewer suggested caching a `MahoSidebarTabListView*` on each row at attach time. This is a performance micro-optimization (parent walk is bounded at ~5 levels and only fires during drag). Out of scope.
5. No change to the `space_header_view_->SetVisible(false)` initial call in `BuildUi()`. The reviewer noted this is dead because `UpdateTabList` will overwrite. Harmless; leaving for explicit safety.
6. No change to `UpdateTabList`'s null-check pattern (`if (space_header_view_) { ... }` vs. internal null-check in `UpdateSidebarSpaceHeaderRow`). Both checks are correct; consolidation is stylistic.
7. No GN file changes. No new files. Lucide migration is the user's parallel work and untouched.

## Current code findings

All paths absolute relative to repo root `/Users/indo/code/project/maho-workspace`.

### Source: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`

`SidebarSpaceHeaderRow` is defined at line 2180. It does NOT have a `METADATA_HEADER` declaration. Its sibling classes do:

- `SidebarSeparatorRow:243` — has `METADATA_HEADER(SidebarSeparatorRow, views::View)`. `BEGIN_METADATA` block at line 486.
- `SidebarInsertionLaneView:502` — same pattern. `BEGIN_METADATA` at 830.
- `MahoSidebarTabListView` — `BEGIN_METADATA` at 833.
- `SidebarTabRowView` — `BEGIN_METADATA` at 835.
- `SidebarFolderRowView` — `BEGIN_METADATA` at 837.

The new class is the only one without metadata. `views::AsViewClass<>` requires metadata to function, so adding it also enables Edit 2 below.

`UpdateSidebarSpaceHeaderRow()` at line 2289-2295 does:
```cpp
void UpdateSidebarSpaceHeaderRow(views::View* row,
                                 const std::string& icon,
                                 const std::u16string& name) {
  if (!row) {
    return;
  }
  static_cast<SidebarSpaceHeaderRow*>(row)->Configure(icon, name);
}
```
The `static_cast` is unchecked. After Edit 1 adds metadata, this becomes `views::AsViewClass<SidebarSpaceHeaderRow>(row)` with a CHECK on the result.

`SidebarAutoScroller::OnDragOver` at line 2322-2344 declares `constexpr int kAutoScrollEdgeDp = 24;` inside the method body. `ScrollTick` at line 2351-2356 declares `constexpr int kAutoScrollStepDp = 4;` similarly. `Start(FROM_HERE, base::Hertz(30), ...)` at line 2334 and 2340 uses an inline `30`.

`SidebarSpaceHeaderRow::OnDragUpdated` at line 2237 has `const ui::DropTargetEvent& event` parameter that is unused (only `drag_over_ = true` and `UpdateAppearance()`).

### Source: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`

`GetCoreFooterSpaceState()` is defined at line 104 as a file-local helper. It is called twice per `BuildViewStateModel()` cycle:
- line 911 (within `BuildTabListModel`)
- line 928 (within `BuildFooterModel`)

Each call performs one FFI roundtrip (`maho_core_get_space_view_models` + `maho_core_get_active_space_id`), one JSON parse, two `maho_string_free()` releases. Cost is small but non-zero, and called on every UI refresh tick.

`BuildTabListModel()` at line 911-919:
```cpp
const auto space_state = IsSidebarSafeMode() ? FooterSpaceState{} : GetCoreFooterSpaceState();
if (space_state.active_space_index >= 0 &&
    space_state.active_space_index <
        static_cast<int>(space_state.space_names.size())) {
  model.active_space_icon =
      space_state.space_icons[space_state.active_space_index];
  model.active_space_name = base::UTF8ToUTF16(
      space_state.space_names[space_state.active_space_index]);
}
```
The `active_space_index` bounds check uses only `space_names.size()`. The subsequent `space_icons[active_space_index]` is unchecked. If Rust-side desyncs the two vectors (unlikely but possible: race, partial update, bug), heap OOB read.

### Source: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.h`

`MahoSidebarStateAdapter` is the class to extend with a per-cycle cache. Need to add a member like:
```cpp
std::optional<FooterSpaceState> cached_space_state_;
```
and a helper to populate-or-return:
```cpp
const FooterSpaceState& EnsureSpaceState();
void ResetCache();
```
Or alternatively, `BuildViewStateModel()` can populate a local `FooterSpaceState` and pass it to both builders explicitly.

### Source: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_dispatch_unittest.cc`

This is the existing contract test file. Adding TODO comment here is the lowest-friction location. No new test logic added; just a placeholder for the future follow-up plan.

## File-by-file implementation plan

Three files modified directly. One file (test) gets a comment-only addition. No new files.

### File 1: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`

#### Edit 1.1: Add file-scope auto-scroll constants

Locate the existing `constexpr int kRow*Dp` constants near the top of the anonymous namespace (around line 88-95). Add three new constants:

```cpp
constexpr int kAutoScrollEdgeDp = 24;
constexpr int kAutoScrollStepDp = 4;
constexpr int kAutoScrollHz = 30;
```

Place them after `kRowTrailingSpacingDp` (line 96) or before `kSectionSpacingDp`, whichever feels grouped with related layout constants.

#### Edit 1.2: Add `METADATA_HEADER` to `SidebarSpaceHeaderRow`

Locate the class definition at line 2180. Insert the metadata header as the first declaration inside the class:

```cpp
class SidebarSpaceHeaderRow : public views::View {
  METADATA_HEADER(SidebarSpaceHeaderRow, views::View)

 public:
  SidebarSpaceHeaderRow() {
    ...
  }
  ...
};
```

#### Edit 1.3: Annotate unused parameter in `OnDragUpdated`

Change line 2237:
```cpp
int OnDragUpdated(const ui::DropTargetEvent& event) override {
```
to:
```cpp
int OnDragUpdated(const ui::DropTargetEvent& /*event*/) override {
```

#### Edit 1.4: Add `BEGIN_METADATA(SidebarSpaceHeaderRow)` block

Locate the closing `};` of `SidebarSpaceHeaderRow` (around line 2274). Immediately after the closing brace, before the next definition (which is `ForwardDragToAutoScroller`), add:

```cpp
BEGIN_METADATA(SidebarSpaceHeaderRow)
END_METADATA
```

#### Edit 1.5: Replace `static_cast` with `views::AsViewClass<>` in `UpdateSidebarSpaceHeaderRow`

Locate the function at line 2289-2295. Replace its body:

```cpp
void UpdateSidebarSpaceHeaderRow(views::View* row,
                                 const std::string& icon,
                                 const std::u16string& name) {
  if (!row) {
    return;
  }
  auto* header = views::AsViewClass<SidebarSpaceHeaderRow>(row);
  CHECK(header) << "UpdateSidebarSpaceHeaderRow received non-SidebarSpaceHeaderRow";
  header->Configure(icon, name);
}
```

#### Edit 1.6: Refactor `SidebarAutoScroller` to use file-scope constants

Locate `OnDragOver` at line 2322-2344 and `ScrollTick` at line 2351-2356.

In `OnDragOver`, remove the inline `constexpr int kAutoScrollEdgeDp = 24;` and use the file-scope constant. Replace `base::Hertz(30)` with `base::Hertz(kAutoScrollHz)`:

```cpp
void OnDragOver(const gfx::Point& screen_point) {
  if (!scroll_view_) {
    return;
  }
  gfx::Point local_point = screen_point;
  views::View::ConvertPointFromScreen(scroll_view_, &local_point);

  const int height = scroll_view_->height();

  if (local_point.y() >= 0 && local_point.y() < kAutoScrollEdgeDp) {
    direction_ = -1;
    if (!timer_.IsRunning()) {
      timer_.Start(FROM_HERE, base::Hertz(kAutoScrollHz), this,
                   &SidebarAutoScroller::ScrollTick);
    }
  } else if (local_point.y() > height - kAutoScrollEdgeDp &&
             local_point.y() <= height) {
    direction_ = 1;
    if (!timer_.IsRunning()) {
      timer_.Start(FROM_HERE, base::Hertz(kAutoScrollHz), this,
                   &SidebarAutoScroller::ScrollTick);
    }
  } else {
    timer_.Stop();
  }
}
```

In `ScrollTick`, remove the inline constant:

```cpp
void ScrollTick() {
  if (!scroll_view_) {
    return;
  }
  scroll_view_->ScrollByOffset(
      gfx::PointF(0, kAutoScrollStepDp * direction_));
}
```

### File 2: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.h`

#### Edit 2.1: Add per-cycle cache members

Locate `MahoSidebarStateAdapter` class declaration. In the private section, add:

```cpp
// Per-update-cycle cache for GetCoreFooterSpaceState() results. Populated
// at the start of BuildViewStateModel() and reused by BuildTabListModel
// and BuildFooterModel within the same call. Reset on each new
// BuildViewStateModel() invocation.
std::optional<FooterSpaceState> cycle_space_state_cache_;
```

If `FooterSpaceState` is currently defined only in the .cc file (as an anonymous-namespace local struct), promote it to the .h file's private section so it can be referenced as a member type. The struct is small (4 vectors of strings + 1 int) so this is straightforward.

Alternative if FooterSpaceState lift-out is undesirable: keep cache local to `BuildViewStateModel` as a stack variable, and pass `const FooterSpaceState&` into both `BuildTabListModel` and `BuildFooterModel`. This avoids the .h change and is the cleaner option. **Choose this alternative.**

### File 2 (revised): `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`

#### Edit 2.1 (revised): Hoist `GetCoreFooterSpaceState()` into `BuildViewStateModel`

Locate `BuildViewStateModel()` (or equivalent top-level update function). Before calling `BuildTabListModel` and `BuildFooterModel`, fetch the space state once:

```cpp
MahoSidebarViewStateModel MahoSidebarStateAdapter::BuildViewStateModel(
    Browser* browser) {
  MahoSidebarViewStateModel state;

  const FooterSpaceState space_state =
      IsSidebarSafeMode() ? FooterSpaceState{} : GetCoreFooterSpaceState();

  state.tab_list = BuildTabListModel(browser, space_state);
  state.footer = BuildFooterModel(browser, space_state);
  ...
  return state;
}
```

Update the signatures of `BuildTabListModel` and `BuildFooterModel` to accept `const FooterSpaceState&` as a parameter. Remove their internal `GetCoreFooterSpaceState()` calls.

If the public method signatures are exposed in the .h file, either:
- (a) Add a parameter to the public methods (breaks any external caller — verify there are none),
- (b) Keep the public no-arg methods and add private overloads `BuildTabListModelImpl(Browser*, const FooterSpaceState&)` that the public methods delegate to.

Use option (b) for safety. Public API unchanged; internal overloads thread the cached state.

#### Edit 2.2: Bounds safety on both vectors in `BuildTabListModel`

Locate the conditional at line 911-919. Replace with explicit dual bounds check:

```cpp
const int idx = space_state.active_space_index;
if (idx >= 0 &&
    idx < static_cast<int>(space_state.space_names.size()) &&
    idx < static_cast<int>(space_state.space_icons.size())) {
  model.active_space_icon = space_state.space_icons[idx];
  model.active_space_name =
      base::UTF8ToUTF16(space_state.space_names[idx]);
}
```

### File 3: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`

No code changes needed beyond what is already in place. The `#include "ui/views/controls/scroll_view.h"` line was inserted at the end of the include block. Per Chromium style, includes should be alphabetically sorted within their grouping. Verify by re-reading the include block.

#### Edit 3.1: Resort include for `ui/views/controls/scroll_view.h`

If currently at the end of the `ui/views/...` block, move it to the alphabetical position. Other `ui/views/` includes typically sit grouped together; `controls/scroll_view.h` should sit after `controls/...` siblings if any, otherwise just in the alphabetical slot.

Run `clang-format` would handle this automatically; manual sorting is fine if clang-format is not available in this workflow.

### File 4: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_dispatch_unittest.cc`

#### Edit 4.1: Add a TODO comment for the future test follow-up

Locate a sensible spot near the existing tests (e.g., right above the contract test at line 2649, or at the end of the file before the closing `}  // namespace`). Insert:

```cpp
// TODO(maho-sidebar-tests): Add tests for SidebarSpaceHeaderRow drop semantics
// (PlanSectionDrop(kPinned, payload)) and SidebarAutoScroller edge-zone math
// (24dp edge, 30Hz, 4dp/tick = 120 dp/s). Tracked in a future follow-up plan.
```

This is a comment-only change. No test logic added. No `BUILD.gn` modification.

## Verification plan

Execute these checks **in order**. Stop at the first failure and report.

### Check 1: LSP diagnostics on all modified files

Run `lsp_diagnostics` in parallel on:
1. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc`
2. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc`
3. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_state_adapter.h` (only if changed)
4. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_view.cc`
5. `maho-chromium/browser/ui/views/sidebar/maho_sidebar_dnd_dispatch_unittest.cc`

Pre-existing Chromium include-path errors are noise. Severity-error count above noise floor must be 0.

### Check 2: Cargo typecheck

Run `cd maho && bun run typecheck` from repo root. Must pass.

### Check 3: Chromium build

Run `python3 maho-chromium/build/scripts/build_maho.py` from repo root.

Expected outcome: EXIT=0 and `libchrome_dll.dylib` relinked. If the build fails on a file unrelated to this plan's changes (such as ongoing lucide migration files), report the failure and stop. Do not modify lucide files.

### Check 4: Read-back

Read each modified file's relevant section. Confirm:
1. `SidebarSpaceHeaderRow` has `METADATA_HEADER(...)` line and `BEGIN_METADATA/END_METADATA` block.
2. `UpdateSidebarSpaceHeaderRow` uses `views::AsViewClass<>` with CHECK.
3. `kAutoScrollEdgeDp`, `kAutoScrollStepDp`, `kAutoScrollHz` are at file scope.
4. `BuildTabListModel` and `BuildFooterModel` no longer call `GetCoreFooterSpaceState()` directly — they receive `const FooterSpaceState&` from the caller.
5. `BuildTabListModel`'s bounds check covers both vectors.
6. `unittest.cc` has the TODO comment.

### Check 5: Manual verification (after build success)

Relaunch the app:
```bash
killall Maho 2>/dev/null
open /Users/indo/code/project/maho-workspace/chromium/src/out/Default/Maho.app
```

Verify:
1. Footer is still visible.
2. Sticky space header still appears in `top_region_` below favorites.
3. Tab list scrolls when content overflows.
4. Drag-auto-scroll still works near edges (24dp, 120 dp/s).
5. Drop on space header still pins the dragged tab.

If any visual regression appears, run `lsp_diagnostics` on changed files and revert to last-known-good.

## Risks and mitigations

### Risk 1: METADATA macro placement requires forward declaration

`METADATA_HEADER(SidebarSpaceHeaderRow, views::View)` requires `SidebarSpaceHeaderRow` to be the class being declared. The macro is placed inside the class body. No forward declaration issue.

`BEGIN_METADATA(SidebarSpaceHeaderRow)` is placed AFTER the class definition. The macro requires the class to be fully defined at that point — which it is, since the closing `};` precedes it.

**Mitigation**: Mirror exactly how `SidebarSeparatorRow` does it (line 243 + line 486). Pattern is well-tested.

### Risk 2: Anonymous namespace + METADATA visibility

`SidebarSpaceHeaderRow` is in an anonymous namespace. `BEGIN_METADATA` registers the class with the Views metadata system. Anonymous namespace classes still register fine — the existing `SidebarSeparatorRow`, `SidebarInsertionLaneView` are in the same anonymous namespace and have working metadata.

**Mitigation**: No special handling needed.

### Risk 3: `views::AsViewClass<SidebarSpaceHeaderRow>` from anonymous-namespace caller

`UpdateSidebarSpaceHeaderRow` is a free function. It needs to reference `SidebarSpaceHeaderRow` (anonymous namespace). Since the function is in the same translation unit and can be defined inside the same anonymous namespace OR after the namespace closes (with the type still visible via using directive `using namespace maho;`), this should resolve. Need to verify the function is in the right scope.

**Mitigation**: Place `UpdateSidebarSpaceHeaderRow` and `CreateSidebarSpaceHeaderRow` definitions inside the anonymous namespace OR keep them where they are now (in `namespace maho`) and use a fully-qualified type if the implicit lookup fails. The existing forward declarations in `namespace maho` at the top of the file may need a corresponding type forward declaration for `SidebarSpaceHeaderRow`. Cleanest path: add `class SidebarSpaceHeaderRow;` forward declaration in the same `namespace maho {}` block at the top, and ensure the actual class is defined BEFORE `UpdateSidebarSpaceHeaderRow` (which it already is — class at line 2180, function at line 2289).

### Risk 4: Public API change on `MahoSidebarStateAdapter`

If `BuildTabListModel` and `BuildFooterModel` are declared public in the .h file, adding a parameter is a breaking change. Mitigation: use private overloads (option b in Edit 2.1 revised). Public no-arg methods stay; internal overloads thread the cached state. No external caller affected.

### Risk 5: `FooterSpaceState` type visibility

If `FooterSpaceState` is defined in the anonymous namespace of `maho_sidebar_state_adapter.cc`, it can be passed between functions in the same TU but not declared in the .h. Mitigation: keep the type local to the .cc; the private overloads (option b) live in the .cc and have visibility. No .h change needed.

### Risk 6: Build cycle pollution

Adding `BEGIN_METADATA` requires `#include "ui/base/metadata/metadata_impl_macros.h"` (or whatever the canonical include is for `BEGIN_METADATA/END_METADATA`). The file already uses `METADATA_HEADER` for other classes, so the include should already be present. Verify before edit.

**Mitigation**: Search for existing `BEGIN_METADATA` in the file (already done — confirmed at lines 486, 830, 833, 835, 837), confirming the include is present. No new include needed.

## Out-of-scope / follow-ups

1. **Test additions** for `SidebarSpaceHeaderRow` drop semantics and `SidebarAutoScroller` edge-zone math. Tracked via TODO comment in `maho_sidebar_dnd_dispatch_unittest.cc`. Future follow-up plan name suggestion: `sidebar-sticky-space-header-tests-plan-YYYY-MM-DD.md`.
2. **Data model deduplication** (remove `MahoSidebarTabListModel.active_space_icon/name` and read from FooterModel). Multi-file refactor. Schedule after the sticky header behavior stabilizes in QA.
3. **Auto-scroll forwarding encapsulation** (`friend` declarations vs. public methods). Stylistic; non-blocking.
4. **Parent-walk caching** in `ForwardDragToAutoScroller`. Performance micro-optimization; defer until profiling shows it as a hotspot.
5. **Dead `space_header_view_->SetVisible(false)`** in `BuildUi()`. Harmless; leave unless cleaning up adjacent code.
6. **`UpdateTabList` null-check style consolidation**. Stylistic; non-blocking.
7. **`SetVerticalScrollBarMode(kHiddenButEnabled)`** to hide scroll bar visually. User decision pending; out of scope unless requested.
