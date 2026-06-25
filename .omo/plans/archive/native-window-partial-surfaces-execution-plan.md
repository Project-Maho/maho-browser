# Native Window Parity: Partial-15 Execution Plan

## Status
- Execution plan derived from completed parity audit
- Scope: bring the 15 surfaces classified as `Partial` to full native-window parity
- Out of scope: `Notification overlay` (classified `Unimplemented`, separate initiative)

## Current State Summary

The native-window parity audit classified 20 surfaces into three tiers:

| Classification | Count | Surfaces |
|---|---|---|
| Complete | 4 | Sidebar, Inline sidebar create-space, Standalone create-space dialog host, Native theme chooser |
| Partial | 15 | See ranked list below |
| Unimplemented | 1 | Notification overlay |

The four Complete surfaces required no further work. The single Unimplemented surface (`Notification overlay`, stub at `maho-chromium/browser/ui/notifications/maho_notification_overlay.cc`) is a greenfield effort tracked separately and is explicitly excluded from this plan.

This document covers the 15 Partial surfaces: each has some native implementation present but gaps in functionality, visual fidelity, lifecycle handling, or integration completeness.

## The Partial-15: Priority-Ranked List

| Priority | Surface | Rationale |
|---|---|---|
| 1 | Command palette overlay | Foundation surface. Blocks unified input, contextual actions, and click-path unification. Already has a dedicated sub-plan; remaining parity gaps are overlay lifecycle and mode-presentation polish. |
| 2 | Spaces overlay | Core navigation primitive. Governs space switching, reordering, and management. High user-frequency, high visibility. |
| 3 | Location bar utility panel/bubble | Central interaction point for page-level actions (permissions, downloads, extensions). Partial state likely means missing bubble types or incomplete anchor/positioning. |
| 4 | Context menus | Cross-cutting surface. Every right-click relies on this. Parity gaps ripple into perceived polish of all other surfaces. |
| 5 | Split view | Distinguishing Maho feature. Partial parity here blocks a key differentiator from shipping cleanly. |
| 6 | AI side panel host | Growing product surface. Needs stable hosting before AI features can iterate freely on content. |
| 7 | Space config dialog | Required for space customization (icon, name, theme re-selection). Blocks complete space-management flow. |
| 8 | Space delete confirmation dialog | Dependency on space config flow. Small surface, but without it the destructive action has no native gate. |
| 9 | Sidebar action popover | Contextual actions on sidebar items. Partial state likely means inconsistent dismiss/anchor behavior or missing action types. |
| 10 | Boost bubble | Per-site boost controls. Lower frequency than core navigation but visible in the boost product story. |
| 11 | Shield bubble | Per-site privacy/blocking controls. Similar scope to Boost bubble; can share positioning/lifecycle patterns. |
| 12 | Little Arc window | Lightweight popup browser window. Complex surface (own top-chrome, tab strip, navigation). Lower priority because it's an auxiliary workflow, not a primary interaction path. |
| 13 | Link preview | Hover/long-press surface. Lower criticality; partial state is tolerable during early shipping. |
| 14 | Tab preview | Hover preview on tab items. Polish surface; partial state doesn't block core workflows. |
| 15 | Page info/site control UI | Certificate/site-info bubble. Chromium provides a baseline; Maho parity work is theming and layout consistency. Lowest urgency. |

## Priority Rationale

The ordering follows three principles:

1. **Foundational surfaces first.** Surfaces that other features depend on (command palette, spaces overlay, location bar) unblock downstream work across the product.
2. **User-frequency weighting.** Surfaces encountered in every session (context menus, split view) rank above surfaces encountered occasionally (link preview, page info).
3. **Shared-pattern grouping.** Surfaces that share implementation patterns (Boost/Shield bubbles, Link/Tab previews) are adjacent so architectural decisions carry forward.

## Implementation Waves

### Wave 1: Core Navigation and Input (Priority 1-4)

**Surfaces:** Command palette overlay, Spaces overlay, Location bar utility panel/bubble, Context menus

**Goal:** The four highest-traffic surfaces reach full parity. After this wave, the most common interactions feel native and complete.

**Subsystem clusters:**
- `maho-chromium/browser/ui/views/command/` (command palette)
- `maho-chromium/browser/ui/views/spaces_overlay/` (spaces overlay)
- `maho-chromium/browser/ui/views/location_bar/` (utility panel/bubble)
- Context menu theming/structure likely touches `maho-chromium/browser/ui/views/` and Chromium's `RenderViewContextMenu` override layer

**Likely parity gaps to close:**
- Overlay lifecycle (open/close/focus/dismiss) consistency
- Geometry and positioning under varied window states
- Visual theming alignment with Maho design language
- Missing action types or incomplete option sets

### Wave 2: Differentiation Features (Priority 5-6)

**Surfaces:** Split view, AI side panel host

**Goal:** Maho's differentiating features ship with native-quality hosting.

**Subsystem clusters:**
- Split view logic likely under `maho-chromium/browser/ui/views/` with frame/layout integration
- AI side panel host under `maho-chromium/browser/ui/views/` or a dedicated `side_panel/` directory

**Likely parity gaps to close:**
- Split view resize handles, drag behavior, and persistence across sessions
- Side panel host frame integration, sizing, and content communication channel

### Wave 3: Space Management (Priority 7-8)

**Surfaces:** Space config dialog, Space delete confirmation dialog

**Goal:** Complete the space lifecycle. Users can create, customize, and delete spaces entirely through native UI.

**Subsystem clusters:**
- `maho-chromium/browser/ui/views/space_create/` or sibling directories for config/delete dialogs
- Integration with `maho-chromium/browser/ui/theme/` for theme persistence on config changes

**Likely parity gaps to close:**
- Missing fields or controls in the config dialog
- Incomplete keyboard handling (Enter to confirm, Escape to cancel)
- Delete confirmation lacking undo affordance or proper space-count messaging

### Wave 4: Contextual Popovers and Bubbles (Priority 9-11)

**Surfaces:** Sidebar action popover, Boost bubble, Shield bubble

**Goal:** Per-item and per-site contextual controls are native and consistent.

**Subsystem clusters:**
- Sidebar action popover near `maho-chromium/browser/ui/views/sidebar/`
- Boost/Shield bubbles likely share a bubble-view base class

**Likely parity gaps to close:**
- Inconsistent anchor positioning across sidebar item types
- Missing action items or disabled states not properly reflected
- Boost/Shield bubbles may lack proper theme-aware coloring or animation

### Wave 5: Auxiliary and Polish Surfaces (Priority 12-15)

**Surfaces:** Little Arc window, Link preview, Tab preview, Page info/site control UI

**Goal:** Auxiliary workflows and hover surfaces reach parity. This wave is the polish pass before declaring full native-window parity.

**Subsystem clusters:**
- Little Arc window is its own mini browser chrome; likely has dedicated view/controller files
- Link/Tab preview surfaces are tooltip-like overlays
- Page info is a Chromium-provided bubble with Maho theme overrides

**Likely parity gaps to close:**
- Little Arc window may lack proper top-chrome or navigation controls
- Preview surfaces may have sizing, timing, or content-rendering gaps
- Page info theming may not match Maho's design language

## Verification Gates

Each wave must satisfy all gate criteria before the next begins. Gates are structured as: build verification, automated test pass, manual scenarios with observable pass criteria.

---

### Wave 1 Gate: Core Navigation and Input

**Build verification:**
```bash
# Full repo build (from workspace root)
python3 maho-chromium/build/scripts/build_maho.py

# Targeted rebuild if iterating (from chromium/src)
autoninja -C out/Default chrome
```
Build must complete with zero errors. Warnings in touched files must not increase.

**Automated tests:**
```bash
# Run interactive UI tests targeting the command, spaces, location bar, and context menu surface clusters
# From chromium/src:
autoninja -C out/Default interactive_ui_tests
./out/Default/interactive_ui_tests --gtest_filter="*Command*:*SpacesOverlay*:*LocationBar*Bubble*:*ContextMenu*"
```
If surface-specific test targets don't yet exist, add at minimum one interactive test per surface that exercises open/dismiss lifecycle, then run. All tests green.

**Manual verification scenarios:**

1. **Command palette overlay:**
   - Open command palette via keyboard shortcut. Type a query. Select a result with arrow keys + Enter. Confirm: overlay dismisses, action executes, focus returns to the page or address bar.
   - Open command palette, then click outside it. Confirm: overlay dismisses without executing any action. No ghost overlay remains.
   - Open command palette in a maximized window, then in a restored/smaller window. Confirm: overlay anchors correctly in both states (no clipping, no offscreen rendering).

2. **Spaces overlay:**
   - Open spaces overlay. Verify all created spaces render with correct names/icons. Switch to a different space. Confirm: overlay dismisses, target space activates, its tabs are visible.
   - Drag-reorder a space within the overlay. Confirm: reorder persists after closing and reopening the overlay.
   - Press Escape while spaces overlay is open. Confirm: overlay dismisses, no space switch occurs.

3. **Location bar utility panel/bubble:**
   - Click a permission indicator (e.g., camera icon) in the location bar. Confirm: bubble appears anchored directly below the icon, not offset or floating.
   - Interact with controls inside the bubble (toggle permission, click a link). Confirm: bubble updates or dismisses as expected, no orphaned bubble remains.
   - Open a bubble, then switch tabs. Confirm: bubble dismisses on tab switch.

4. **Context menus:**
   - Right-click on a page link, on selected text, and on an image. Confirm: each context produces the correct menu items (no missing entries, no empty slots).
   - Navigate context menu entries with keyboard (arrow keys, Enter to select, Escape to dismiss). Confirm: full keyboard operation works.
   - Verify menu theming: background color, text color, hover highlight, and separator lines match Maho design language (no Chromium-default gray leaking through).

**Pass criteria:** All four surfaces open/close without lifecycle bugs across repeated rapid invocations. No WebUI fallback surfaces appear. No focus traps. Visual theming is consistent with Maho's design system.

---

### Wave 2 Gate: Differentiation Features

**Build verification:**
```bash
python3 maho-chromium/build/scripts/build_maho.py
```
Zero build errors.

**Automated tests:**
```bash
# From chromium/src:
autoninja -C out/Default interactive_ui_tests
./out/Default/interactive_ui_tests --gtest_filter="*SplitView*:*SidePanel*:*AiPanel*"
```
If no matching tests exist yet, add targeted interactive tests covering split-view creation/resize and side-panel show/hide, then run. All green.

**Manual verification scenarios:**

1. **Split view:**
   - Activate split view (via command palette or menu). Confirm: window divides into two panes, each rendering its own tab content. Resize handle is visible.
   - Drag the resize handle. Confirm: both panes resize fluidly with no flicker or content clipping.
   - Close the browser with split view active, relaunch. Confirm: split view state restores (both panes, their URLs, and the split ratio persist).

2. **AI side panel host:**
   - Trigger the AI side panel (via toolbar button or command). Confirm: panel appears on the designated side, properly sized, with content loading inside.
   - Resize the main window to a narrow width. Confirm: side panel either adapts gracefully or collapses without overlapping page content.
   - Close the side panel. Confirm: focus returns to the main content area. No ghost frame or empty panel strip remains.

**Pass criteria:** Split view persists across sessions and resizes correctly. AI side panel hosts content without clipping, focus steal, or orphaned frames. No visual glitches during panel open/close transitions.

---

### Wave 3 Gate: Space Management

**Build verification:**
```bash
python3 maho-chromium/build/scripts/build_maho.py
```
Zero build errors.

**Automated tests:**
```bash
# From chromium/src:
autoninja -C out/Default interactive_ui_tests
./out/Default/interactive_ui_tests --gtest_filter="*SpaceConfig*:*SpaceDelete*"
```
If tests don't exist yet, add tests covering: dialog open, field edit + confirm, delete confirm + cancel. All green.

**Manual verification scenarios:**

1. **Space config dialog:**
   - Open a space's config dialog. Change the space name, select a new icon, choose a different theme. Press Enter or click Confirm. Confirm: dialog closes, space immediately reflects all changes (name in sidebar, icon updated, theme applied).
   - Open config dialog, press Escape. Confirm: no changes persist. The space retains its prior state.
   - Tab through all interactive elements in the dialog. Confirm: focus order is logical (name field → icon picker → theme selector → confirm/cancel buttons).

2. **Space delete confirmation dialog:**
   - Trigger delete on a space with multiple tabs. Confirm: confirmation dialog appears, clearly states how many tabs will close or which space is being removed.
   - Click Cancel on the delete dialog. Confirm: dialog closes, space remains untouched.
   - Click Confirm. Confirm: space is removed, its tabs close, and the browser switches to an adjacent space. No crash, no orphaned empty state.

**Pass criteria:** Full space lifecycle (create → configure → delete) completes through native Views dialogs only. No WebUI fallback surfaces appear at any step. Keyboard handling (Enter to confirm, Escape to cancel) works in both dialogs.

---

### Wave 4 Gate: Contextual Popovers and Bubbles

**Build verification:**
```bash
python3 maho-chromium/build/scripts/build_maho.py
```
Zero build errors.

**Automated tests:**
```bash
# From chromium/src:
autoninja -C out/Default interactive_ui_tests
./out/Default/interactive_ui_tests --gtest_filter="*SidebarAction*:*Boost*Bubble*:*Shield*Bubble*"
```
If surface-specific tests are missing, add targeted bubble lifecycle tests (show, interact, dismiss-on-focus-loss). All green.

**Manual verification scenarios:**

1. **Sidebar action popover:**
   - Right-click (or long-press) a sidebar item. Confirm: popover appears anchored to that item, not floating at a generic position.
   - Click an action in the popover. Confirm: action executes, popover dismisses.
   - Open the popover, then click anywhere else in the browser. Confirm: popover dismisses cleanly on focus loss.

2. **Boost bubble:**
   - Click the Boost indicator for a site. Confirm: bubble appears anchored to the indicator icon, showing current boost level and controls.
   - Adjust boost level within the bubble. Confirm: change takes effect immediately (page re-renders with new boost). Bubble remains open during interaction.
   - Press Escape. Confirm: bubble dismisses. Boost state persists (not reverted on dismiss).

3. **Shield bubble:**
   - Click the Shield icon for a site. Confirm: bubble shows current blocking/privacy status with toggleable controls.
   - Toggle a shield setting. Confirm: page reloads or updates to reflect the new state. Bubble remains stable during toggle.
   - Open Shield bubble, switch to another tab, switch back. Confirm: bubble is dismissed (not persisting across tab switches).

**Pass criteria:** All three surfaces anchor correctly relative to their trigger element. Keyboard navigation within each bubble works (Tab between controls, Escape to dismiss). Focus returns to the trigger element after dismiss. No bubble persists across tab switches or window state changes.

---

### Wave 5 Gate: Auxiliary and Polish Surfaces

**Build verification:**
```bash
python3 maho-chromium/build/scripts/build_maho.py
```
Zero build errors.

**Automated tests:**
```bash
# From chromium/src:
autoninja -C out/Default interactive_ui_tests
./out/Default/interactive_ui_tests --gtest_filter="*LittleArc*:*LinkPreview*:*TabPreview*:*PageInfo*"
```
Add surface-cluster tests if missing. All green.

**Manual verification scenarios:**

1. **Little Arc window:**
   - Trigger a Little Arc window (e.g., via "Open in Little Arc" action). Confirm: a lightweight popup window appears with its own address bar and navigation controls.
   - Navigate to a new URL within the Little Arc window. Confirm: navigation works, page renders correctly, back/forward buttons function.
   - Close the Little Arc window. Confirm: it closes cleanly, no orphaned process or ghost frame in the main window's tab strip.

2. **Link preview:**
   - Hover over (or long-press) a link on a page. Confirm: a preview surface appears showing destination page content or metadata after the expected delay.
   - Move the cursor away from the link. Confirm: preview dismisses after a short timeout, no flicker or stuck preview.
   - Hover a link near the edge of the window. Confirm: preview positions itself within viewport bounds (no offscreen clipping).

3. **Tab preview:**
   - Hover over a background tab in the tab strip. Confirm: a preview thumbnail appears showing that tab's current content.
   - Move quickly across multiple tabs. Confirm: previews update to match the currently hovered tab without visual stacking or flicker.
   - Hover a tab, then click it. Confirm: preview dismisses and the tab activates.

4. **Page info / site control UI:**
   - Click the lock/site icon in the address bar. Confirm: page info bubble appears with certificate status, permissions list, and cookies info.
   - Verify visual theming: fonts, colors, and spacing match Maho design language (no raw Chromium styling).
   - Dismiss the bubble (click outside or press Escape). Confirm: clean dismiss, focus returns to address bar.

**Pass criteria:** Little Arc window navigates independently and closes cleanly. Preview surfaces appear/dismiss based on hover timing without stacking artifacts. Page info bubble is fully themed. No surface in this wave falls back to a WebUI equivalent.

---

### Final Acceptance Gate

All 15 surfaces must satisfy:
1. `python3 maho-chromium/build/scripts/build_maho.py` completes with zero errors.
2. Full `interactive_ui_tests` suite passes (no regressions in unrelated surfaces).
3. Each surface is manually verified against its wave scenarios one final time.
4. All 15 surfaces are reclassified from `Partial` to `Complete` in an updated parity audit.
5. No surface that was previously `Complete` has regressed.

## Notification Overlay: Explicit Exclusion

`Notification overlay` is classified `Unimplemented` (not `Partial`). It has a stub file at `maho-chromium/browser/ui/notifications/maho_notification_overlay.cc` but no functional implementation. It is not part of the Partial-15 execution sequence and requires its own greenfield design/implementation plan.

## Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Wave 1 surfaces have deep Chromium integration dependencies | Longer stabilization time | Timebox each surface. Isolate parity work from upstream merge conflicts. |
| Context menu parity requires extensive upstream override work | Broad surface area, easy to miss entries | Enumerate all menu types early. Accept incremental coverage. |
| Split view interacts with tab strip and frame layout | Changes may destabilize unrelated surfaces | Feature-flag split view work. Test frame layout regressions explicitly. |
| Little Arc window is architecturally complex | May require significant new code, not just parity fixes | If scope balloons, defer to a separate plan rather than blocking Wave 5 completion. |
| Shared bubble patterns (Boost/Shield) may not generalize cleanly | Duplicated effort if implementations diverge | Extract a common `MahoBubbleView` base early in Wave 4. |

## Execution Constraints

- Each wave is estimated at 1-2 weeks of focused implementation work.
- Waves 1 and 2 are sequential (Wave 2 may depend on overlay patterns established in Wave 1).
- Waves 3, 4, and 5 can potentially overlap if engineers are available, since they touch independent subsystems.
- No surface should be marked Complete without both automated test coverage and manual QA confirmation.

## Practical Bottom Line

This plan converts a static audit into a sequenced, verifiable execution path. The ordering is designed so that each wave either unblocks later work or addresses the highest-visibility gaps first. After all five waves ship and pass verification, the parity audit can be re-run with the expectation that all 15 Partial surfaces move to Complete.
