# Space Switcher Swift Parity Plan

**Date:** 2026-05-06  
**Branch:** main  
**Scope:** `maho-chromium/browser/ui/views/sidebar/maho_sidebar_footer_view.*` primary, plus `maho_sidebar_action_popover_view.*` and new files as needed.  
**Reference:** `maho/macos-shell/Sources/SpaceSwitcher/` (AppKit implementation)

---

## Objective

Bring the Chromium fork's sidebar footer space switcher to visual and behavioral parity with the Swift reference implementation. The Swift code is the canonical design; Chromium must match its UX paradigm, not the other way around.

---

## Current State Summary

The Chromium footer currently renders each space as a **18×18 emoji LabelButton** inside a tinted cluster container. The Swift reference renders each space as a **4–8px color dot** (with hover-to-icon transition) inside a **scrollable strip**. These are fundamentally different visual paradigms.

### What Chromium Has That Swift Does Not
- `update_pill_` (version status indicator) — **intentional addition, keep as-is**
- Cluster container with tinted background and border — **design decision to evaluate**

### What Swift Has That Chromium Does Not
- Color-dot rendering with state-based size changes (4/4.5/8px)
- Hover → icon cross-fade transition
- Horizontal scroll with fade-edge masks
- Drag & drop for space reordering
- Keyboard navigation (arrow keys, Return, Cmd+-)
- Per-space context menu (right-click individual space)
- Profile badge (multi-profile indicator)
- Action popover with SF Symbol icons and keyboard navigation

---

## Serial Gates

### Gate 0 — Extract SpaceSwitcherDotView as a standalone class

**Purpose:** The current `RebuildDots()` creates anonymous `views::LabelButton` instances inline. To implement state-based rendering (color dot, hover icon, size animation), each space needs its own view class with internal state management.

**Why gated:** Every subsequent visual change (dot sizing, hover, icon, drag) requires a proper per-item view. Without this, all other items are blocked.

**Target files:**  
- New: `maho_sidebar_space_dot_view.h`, `maho_sidebar_space_dot_view.cc`  
- Modified: `maho_sidebar_footer_view.cc` (`RebuildDots` → uses new class)

**Work:**
1. Create `MahoSidebarSpaceDotView : public views::View` with:
   - Owned data: space_id, color, icon, name, is_active, is_selected, is_hovered
   - Internal sub-views: color badge circle, icon label (emoji), icon image (future: vector icon)
   - `SetActive(bool)`, `SetSelected(bool)` methods that trigger size/color recalc
   - Mouse enter/exit tracking for hover state
   - `OnMousePressed` → delegate callback for activation
   - `OnMouseDragged` → delegate callback for drag initiation (stub, implemented in Gate 3)
   - Right-click → delegate callback for context menu
2. Migrate `RebuildDots` to instantiate `MahoSidebarSpaceDotView` instead of `views::LabelButton`
3. Overall container size: 20×20 per item. Inner indicator: 16×16.

**Oracle Pass Criteria:**
- [ ] `MahoSidebarSpaceDotView` is a concrete `views::View` subclass, not a `views::Button`
- [ ] No rendering logic remains inline in `RebuildDots` — it only creates/configures dot views
- [ ] Each dot view holds its own space_id, color, active/selected/hover state
- [ ] Mouse tracking area is registered for hover detection
- [ ] Delegate pattern (or callback) used for click/right-click/drag — dot view does not call bridge directly
- [ ] Existing footer interactive tests still pass (`maho_sidebar_spaces_interactive_test.cc`)
- [ ] `GetSpaceDotForTesting(int)` returns the new dot view type successfully
- [ ] `lsp_diagnostics` clean on all changed/new files

---

### Gate 1 — Color Dot Rendering with State-Based Sizing

**Purpose:** Replace the emoji-button paradigm with the Swift reference's color-dot paradigm where each space is a small colored circle whose size reflects state.

**Why gated:** This is the core visual identity change. Hover-to-icon (Gate 2) builds on top of the dot as the base state.

**Target files:**  
- `maho_sidebar_space_dot_view.cc`

**Work:**
1. Render a filled circle (SkPath or rounded rect with radius=size/2) as the primary visual:
   - Default: 4px diameter
   - Selected: 4px diameter (same as default in Swift)
   - Hovered (no icon): 4.5px diameter
   - Active: 8px diameter
2. Color calculation: port `SpaceThemeManager.itemAppearance()` logic or use the existing `BlendTint` as baseline but apply it to the dot fill, not a button background
3. Remove the emoji LabelButton background. The dot IS the visual, not a background behind text.
4. Active space: NO border. Size increase to 8px is the only active indicator (matching Swift).
   - Remove the current `views::CreateRoundedRectBorder` for active state

**Oracle Pass Criteria:**
- [ ] Default state renders a 4px colored circle, not an 18px emoji button
- [ ] Active state renders an 8px colored circle, no border
- [ ] Hovered state (without icon) renders a 4.5px circle
- [ ] Color is derived from space color, not from system surface color
- [ ] No emoji text visible in default (non-hovered) state
- [ ] Visual diff from screenshot matches Swift reference layout (dots in a row, not emoji buttons)
- [ ] `lsp_diagnostics` clean

---

### Gate 2 — Hover-to-Icon Transition

**Purpose:** When hovering a space dot that has an icon (emoji or SF Symbol name), cross-fade the color dot out and the icon in.

**Why gated:** Requires Gate 1's dot rendering and Gate 0's hover state tracking.

**Target files:**  
- `maho_sidebar_space_dot_view.cc`

**Work:**
1. On hover enter:
   - If space has icon string:
     - If icon is emoji: show emoji text centered in 16×16 indicator, set dot alpha to 0
     - If icon is SF Symbol name: show vector icon (or skip — Chromium doesn't have SF Symbols natively; use emoji fallback or Chromium vector icons)
   - If space has no icon: enlarge dot to 4.5px
2. On hover exit:
   - Hide icon, restore dot alpha to 1.0, restore dot to default/active size
3. Animate the transition: target 160ms ease-in-out using `views::AnimationBuilder` or `gfx::Animation`
4. During hover with icon, set item alpha to 1.0 (active) or 0.9 (inactive) matching Swift

**Oracle Pass Criteria:**
- [ ] Hovering a space with an emoji icon hides the color dot and shows the emoji
- [ ] Hovering a space without an icon enlarges the dot to 4.5px
- [ ] Leaving hover restores the original dot state
- [ ] Transition is animated (not instant snap), visually smooth
- [ ] No flickering when moving mouse between adjacent dots rapidly
- [ ] `lsp_diagnostics` clean

---

## Parallel Work (after Gate 0, independent of each other)

### P1 — Horizontal Scroll with Fade Mask

**Purpose:** When space count exceeds available width, enable horizontal scrolling with gradient fade at edges.

**Target files:**  
- `maho_sidebar_footer_view.cc` — wrap `dot_container_` in a `views::ScrollView`

**Work:**
1. Replace direct `dot_container_` embedding with a `views::ScrollView`:
   - Horizontal scroll only
   - Hide scrollbar (`SetHorizontalScrollBarMode(views::ScrollView::ScrollBarMode::kHiddenButEnabled)`)
2. Implement fade mask using `ui::LayerDelegate` or a gradient overlay view:
   - Left fade: when scroll offset > 2px
   - Right fade: when scroll offset < max - 2px
   - Fade distance: 12px
3. Update scroll position when active space changes (scroll-to-visible)

**Oracle Pass Criteria:**
- [ ] With 2-3 spaces, no scrollbar or fade is visible; dots are centered
- [ ] With 10+ spaces, horizontal scrolling works via mouse wheel/trackpad
- [ ] Left fade appears only when scrolled away from start
- [ ] Right fade appears only when more content exists beyond visible area
- [ ] Switching to a non-visible space auto-scrolls it into view
- [ ] Fade is gradient (not hard edge), matching Swift's `CAGradientLayer` aesthetic
- [ ] `lsp_diagnostics` clean

---

### P2 — Drag & Drop for Space Reordering

**Purpose:** Allow users to reorder spaces by dragging dots within the strip.

**Target files:**  
- `maho_sidebar_space_dot_view.cc` — drag source
- `maho_sidebar_footer_view.cc` — drop target
- New: drag data format registration

**Work:**
1. In `MahoSidebarSpaceDotView`:
   - Track `mouseDown` position
   - On `mouseDragged`, if distance > 4px, initiate drag with space_id as payload
   - Use `ui::OSExchangeData` with custom clipboard format `"maho/space-switcher-drag"`
2. In `MahoSidebarFooterView` (or a scroll wrapper):
   - Register as drop target for the custom format
   - On drag over: calculate insertion index from mouse X position relative to dot midpoints
   - Show a 2px drop indicator view at the insertion position
   - On drop: call `bridge->ReorderSpace(space_id, from_index, to_index)`
3. Drop indicator: 2px wide, full dot height, themed color

**Oracle Pass Criteria:**
- [ ] Dragging starts only after 4px mouse movement (no accidental drags)
- [ ] Drop indicator appears at correct insertion position between dots
- [ ] Dropping reorders the space and the UI updates to reflect new order
- [ ] Dropping at the same position is a no-op
- [ ] Dragging outside the strip cancels the operation cleanly
- [ ] Single space cannot be dragged (requires count > 1)
- [ ] `lsp_diagnostics` clean
- [ ] Interactive test covers: drag space from position 0 to position 2

---

### P3 — Keyboard Navigation

**Purpose:** Allow keyboard-driven space switching within the footer.

**Target files:**  
- `maho_sidebar_footer_view.cc` — key event handling

**Work:**
1. When footer (or dot container) has focus:
   - Left arrow: select previous space, activate it
   - Right arrow: select next space, activate it
   - Return/Space: activate currently selected space
   - Cmd+N or configured shortcut: open add-space popover
2. Track `selectedSpaceId` separately from `activeSpaceId` (matching Swift's separation)
3. Visually distinguish selected (4px) from active (8px) — already handled by Gate 1 if sizing is correct

**Oracle Pass Criteria:**
- [ ] Left/right arrow keys move selection and activate space
- [ ] Selection wraps or clamps at boundaries (Swift clamps, not wraps)
- [ ] Return key activates the selected space
- [ ] Focus is visually indicated (focus ring or highlight on the selected dot)
- [ ] Keyboard nav works after clicking a dot, then using arrow keys
- [ ] `lsp_diagnostics` clean

---

### P4 — Per-Space Context Menu

**Purpose:** Right-clicking an individual space dot should show that space's context menu, not a generic spaces menu.

**Target files:**  
- `maho_sidebar_space_dot_view.cc` — right-click handler
- `maho_sidebar_footer_view.cc` — remove cluster-level context menu, delegate to dots

**Work:**
1. `MahoSidebarSpaceDotView` receives right-click, calls delegate with space_id
2. Footer view builds `MahoSpaceContextMenu` for the specific space (not "sidebar-spaces")
3. Remove `spaces_cluster_->set_context_menu_controller(this)` — context menu now lives on individual dots

**Oracle Pass Criteria:**
- [ ] Right-clicking space dot opens a menu specific to THAT space (rename, delete, configure)
- [ ] Right-clicking empty area in the cluster does NOT open a menu
- [ ] The menu actions operate on the correct space (not always the active one)
- [ ] `lsp_diagnostics` clean

---

### P5 — Action Popover Icon & Keyboard Enhancement

**Purpose:** Add icons to the action popover rows and enable keyboard navigation inside the popover.

**Target files:**  
- `maho_sidebar_action_popover_view.cc`

**Work:**
1. **Icons:** Add a Chromium vector icon (or emoji fallback) to each action row:
   - New Space: grid icon (or 🔲)
   - New Folder: folder icon (or 📁)  
   - New Tab: tab icon (or ➕)
   - Render as a 28×28 background tile with 16×16 icon centered, matching Swift's `SpaceSwitcherActionRowView`
2. **Row layout:** Change from single `LabelButton` with `\n` text to a proper layout:
   - `[28×28 icon tile] [title / subtitle stack]` horizontal
   - Title: bold 13px, Subtitle: medium 11px — separate labels
3. **Keyboard:** Override `OnKeyPressed`:
   - Up/Down arrows: highlight navigation
   - Tab: cycle with optional wrap
   - Return/Space: activate highlighted item
   - Escape: close popover
4. **Hover highlight:** Track highlighted index, apply background color on highlighted row

**Oracle Pass Criteria:**
- [ ] Each popover row has an icon to the left of the text
- [ ] Title and subtitle are separate labels with different font weights/sizes
- [ ] Up/down arrows move highlight between rows
- [ ] Return activates the highlighted row
- [ ] Escape closes the popover without action
- [ ] Hover changes the highlighted row
- [ ] Visual appearance matches Swift reference (272px card, rounded corners, icon tiles)
- [ ] `lsp_diagnostics` clean

---

### P6 — Profile Badge (Low Priority)

**Purpose:** Show a small colored badge on dots when multiple profiles exist, indicating which profile a space belongs to.

**Target files:**  
- `maho_sidebar_space_dot_view.cc`

**Work:**
1. Add a 4×4 sub-view at bottom-right of the indicator container
2. Query profile count from bridge; show badge only when count ≥ 2
3. Color from profile's avatar color
4. 1px border matching window background for visual separation

**Oracle Pass Criteria:**
- [ ] Badge is invisible with single profile
- [ ] Badge appears at bottom-right of dot when 2+ profiles exist
- [ ] Badge color matches the profile's avatar color
- [ ] Badge has 1px border for separation from dot
- [ ] `lsp_diagnostics` clean

---

## Implementation Order

```
Gate 0 (extract dot view class)
  ├── Gate 1 (color dot rendering)  ──→  Gate 2 (hover-to-icon)
  ├── P1 (scroll + fade)
  ├── P2 (drag & drop)
  ├── P3 (keyboard nav)
  ├── P4 (per-space context menu)
  └── P6 (profile badge)

Independent:
  └── P5 (action popover enhancement)
```

Gate 0 → Gate 1 → Gate 2 is strictly serial.  
P1–P4, P6 can start after Gate 0.  
P5 is fully independent.

---

## Files Touched (Predicted)

| File | Change Type |
|---|---|
| `maho_sidebar_space_dot_view.h` | **New** |
| `maho_sidebar_space_dot_view.cc` | **New** |
| `maho_sidebar_footer_view.h` | Modified (dot container type, scroll view, keyboard) |
| `maho_sidebar_footer_view.cc` | Modified (RebuildDots refactor, scroll, drag target, keyboard) |
| `maho_sidebar_action_popover_view.h` | Modified (keyboard, icon) |
| `maho_sidebar_action_popover_view.cc` | Modified (row layout, icon, keyboard) |
| `maho_sidebar_spaces_interactive_test.cc` | Modified (update for new view types, add drag/keyboard tests) |
| `BUILD.gn` (sidebar) | Modified (add new source files) |

---

## Out of Scope

- Update pill: Chromium-only intentional feature; not in Swift reference, keep as-is
- Cluster container background/border: Evaluate after Gate 1 visual is in place. May keep or remove based on look.
- WebUI sidebar (`app.ts`): Separate surface, not covered by this plan
- SF Symbol rendering in Chromium: Use emoji or Chromium vector icons as substitute
- `SpaceThemeManager` full port: Use simplified color derivation initially; full theme system is a separate effort
