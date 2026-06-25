# Maho AI Compact Panel — Dia-Style 4-Icon Topbar

**Status:** New plan, written 2026-05-24. Builds on
`.sisyphus/plans/maho-ai-compact-panel-card-removal-plan.md` (which
already removed the brand text strip and added a corner-floating action
cluster). This plan replaces the corner cluster with a full-width
Dia-style topbar.

## 1. Objective

Replace the current top-right floating action cluster with a full-width
horizontal topbar in normal document flow, distributing **4 action
icons** Dia-style:

| Position | Icon | Action |
|---|---|---|
| **Left** | compose / pencil | Start new session |
| **Right 1** | personalization (sparkle / face) | Open Maho AI settings page |
| **Right 2** | sidebar/window toggle | View mode chooser — sidebar vs floating window |
| **Right 3** | X | Close panel |

No background tint, no backdrop blur, no border on the topbar itself —
just a thin horizontal strip above the conversation thread.

## 2. Source of truth

This file. References:
- `.sisyphus/plans/maho-ai-compact-panel-card-removal-plan.md` — earlier
  card removal + header hide pass that left the corner-floating cluster
  in place. This plan supersedes that cluster.
- Reference image provided by user (Dia browser screenshot, 2026-05-24).

## 3. Verified facts

| Fact | Evidence |
|---|---|
| Current actions live in `CompactFloatingActions` (was `CompactHeader`) | `react/features/compact/compact-header.tsx` |
| CSS for floating cluster sits in `compact-panel.css` | `.compact-panel__floating-actions { position: absolute; … }` |
| `ClosePanel()` mojom method already exists | `maho_ai.mojom`, called from React via `pageHandler.closePanel()` |
| No mojom method exists for "open settings" or "view mode" today | grep `OpenSettings\|SetViewMode\|OpenInPopup` returns 0 hits |
| Maho settings live at `chrome://maho-settings/` | `webui_url_constants.h` |
| Chromium side panels can host arbitrary WebUI URLs; popup windows can also host `chrome://` URLs given appropriate browser-side wiring | Chromium standard |

## 4. Non-goals

1. Do **not** redesign the welcome state copy, composer, conversation
   thread, thinking bubble, or tool-call rendering. Topbar only.
2. Do **not** introduce design tokens. Reuse what `tokens.css` ships.
3. Do **not** invent a new mode beyond "sidebar" / "floating". Other
   modes (popout per-tab, picture-in-picture, etc.) are out of scope.
4. Do **not** touch the developer mode shell — its top chrome is
   handled separately (`compact-panel--developer`).
5. Do **not** add OAuth or auth flows — `personalization` opens the
   existing settings page; it does not become an auth surface.

## 5. Phased migration

Each phase is independently shippable. A→B→C are the small, low-risk
half. D is sizable and may merit its own commit / review cycle.

### Phase A — Topbar CSS layout

**Goal:** `.compact-panel__floating-actions` (corner, absolute) becomes
`.compact-panel__topbar` (full-width, in flow). Existing two icons stay
visible during this phase; their actions are unchanged.

**Files:**
- `maho-chromium/browser/resources/maho_ai/styles/compact-panel.css`

**Atomic steps:**
1. Remove the existing `.compact-panel__floating-actions { … }` rule
   set added in the prior plan (absolute positioning, backdrop-filter,
   background tint, etc.). Replace with:
   ```css
   .compact-panel__topbar {
     display: flex;
     justify-content: space-between;
     align-items: center;
     gap: var(--maho-ai-space-2);
     padding: var(--maho-ai-space-2);
     /* No background, border, or box-shadow — bar is invisible. */
   }
   .compact-panel__topbar-left,
   .compact-panel__topbar-right {
     display: flex;
     align-items: center;
     gap: var(--maho-ai-space-1);
   }
   ```
2. Drop the `padding-top: calc(...)` patch on `.compact-thread__list*`
   that was needed only because the floating cluster overlapped the
   scroll region. With the topbar now in flow, no overlap → no padding
   patch needed.
3. Quick visual sanity: the topbar height (`padding * 2 + button
   height ≈ 28-36px`) should match the panel's expected top inset.

**Verification:**
- Build passes (CSS only, GN regen trivial).
- `getComputedStyle(document.querySelector('.compact-panel__topbar'))`
  reports `position: static`, `display: flex`,
  `justify-content: space-between`, `background-color: rgba(0,0,0,0)`.

**Rollback:** revert the CSS commit.

### Phase B — Close panel button

**Goal:** Add a Close (X) icon to the right group. Wire to existing
`ClosePanel()` mojom method.

**Files:**
- `maho-chromium/browser/resources/maho_ai/react/features/compact/compact-header.tsx`
  (component now better named `compact-topbar.tsx` — plan §5.E covers the
  rename)
- `maho-chromium/browser/resources/maho_ai/store.ts` — expose a
  `closePanel()` action that calls `pageHandler.closePanel()` if not
  already wired.

**Atomic steps:**
1. In the React component, restructure the JSX:
   ```tsx
   <div className="compact-panel__topbar">
     <div className="compact-panel__topbar-left">
       <IconButton symbol="newSession" ariaLabel="Start new session"
                   onClick={onStartSession} />
     </div>
     <div className="compact-panel__topbar-right">
       {/* Phase C inserts personalization here */}
       {/* Phase D inserts view mode chooser here */}
       <IconButton symbol="xmarkSmall" ariaLabel="Close panel"
                   onClick={onClosePanel} />
     </div>
   </div>
   ```
2. Add `onClosePanel: () => void` to props; have the call site (compact
   shell) wire it to `store.closePanel()` which forwards to the mojom
   `ClosePanel()` call.
3. `newSession` icon: pick from existing icon set. The current
   `panelSpark` does not match Dia's pencil aesthetic; switch to `edit`
   (the pencil glyph already in `icons.ts`) for the new-session button.

**Verification:**
- Build passes.
- Live: clicking the X dismisses the side panel host (the underlying
  `ClosePanel` already handles this).

**Rollback:** revert the React commit; `ClosePanel()` mojom is
unaffected.

### Phase C — Personalization (open settings)

**Goal:** Add the personalization icon to the right group, immediately
left of the X. Clicking it opens `chrome://maho-settings/`.

**Files:**
- `maho-chromium/browser/resources/maho_ai/react/features/compact/compact-header.tsx`
- `maho-chromium/browser/ui/webui/maho_ai/maho_ai.mojom` (new method
  `OpenSettings()`)
- `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.{cc,h}`

**Atomic steps:**
1. **Mojom:** add `OpenSettings();` to `interface PageHandler {}`.
   Single method, no response, no params. (Matching the shape of the
   existing `ClosePanel()`.)
2. **C++ page handler:** implement `OpenSettings()`:
   ```cpp
   void MahoAIPageHandler::OpenSettings() {
     DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
     if (!browser_) {
       return;
     }
     content::OpenURLParams params(
         GURL("chrome://maho-settings/"),
         content::Referrer(),
         WindowOpenDisposition::SINGLETON_TAB,
         ui::PAGE_TRANSITION_AUTO_TOPLEVEL,
         /*is_renderer_initiated=*/false);
     browser_->OpenURL(params, /*navigation_handle_callback=*/{});
   }
   ```
   - `SINGLETON_TAB`: focuses the existing settings tab if open, else
     opens a new one in the foreground.
3. **React:** add an `onOpenSettings` prop, pass through to the topbar
   component, render an IconButton with `symbol="settings-gear"` and
   ariaLabel `"Personalization"`.
4. **Store:** add `openSettings()` action that calls
   `pageHandler.openSettings()`.

**Verification:**
- Build passes (mojom regen runs).
- Live: clicking the gear opens chrome://maho-settings as a new tab in
  the same browser; clicking again focuses the same tab (singleton
  behavior).

**Rollback:** drop the new mojom method, the C++ implementation, the
React handler. No persisted state.

### Phase D — View mode chooser (sidebar / floating)

**Goal:** The middle right icon is a small chevron/popout glyph. On
click, surfaces a tiny menu with two options: "Sidebar" (current,
default) and "Floating window". On switching to floating, the panel
opens a detached popup window hosting `chrome://maho-ai/` and closes
the side panel host. On switching back to sidebar, the popup closes
and the side panel re-opens.

This is the heaviest phase. Treat it as its own change set.

**Files:**
- `maho-chromium/browser/resources/maho_ai/react/features/compact/compact-header.tsx`
  (or new file: `view-mode-menu.tsx`)
- `maho-chromium/browser/ui/webui/maho_ai/maho_ai.mojom`
- `maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.{cc,h}`
- `maho-chromium/browser/ui/webui/maho_ai_prefs.h`
- `maho-chromium/browser/ui/webui/maho_ai/maho_ai_prefs_registration.cc`
- (potentially) `maho-chromium/browser/ui/webui/maho_ai/maho_ai_ui.{cc,h}`
  if popup hosting requires distinct registration

**Atomic steps:**

1. **Pref:** add `kAiViewMode` (string, default `"sidebar"`).
   Allowed values: `"sidebar"`, `"floating"`. Register in
   `maho_ai_prefs_registration.cc`.
2. **Mojom:**
   ```mojom
   enum ViewMode { kSidebar, kFloating };
   GetViewMode() => (ViewMode mode);
   SetViewMode(ViewMode mode);
   ```
3. **C++ page handler:**
   - `GetViewMode()`: read pref, return enum.
   - `SetViewMode(mode)`:
     - Persist pref.
     - If new mode is `kFloating` and current host is the side panel:
       - Open `chrome://maho-ai/` as a popup window via
         `Browser::OpenURL` with `WindowOpenDisposition::NEW_POPUP`.
       - The popup window's bounds: pick a default (e.g. 480 × 720),
         positioned near the right edge of the active display.
       - Close the side panel host (best-effort: call `ClosePanel()`
         after a short delay so the popup commits before the side
         panel disappears).
     - If new mode is `kSidebar` and current host is the popup:
       - Trigger side-panel re-open via the existing side panel UI
         API (`SidePanelUI::OpenInActiveTab` or equivalent) for the
         maho-ai entry.
       - Close the current popup (window.close in JS, or browser-side
         close via `Browser::window()->Close()` on the popup browser).
4. **WebUI registration:** verify that `chrome://maho-ai/` works
   when navigated to in a popup window. It should — WebUI URLs are
   browser-process-registered and not host-bound. If something
   side-panel-specific is hard-coded (sidebar inset CSS, etc.), gate it
   on a query string `?host=popup` or via a runtime detection (read
   `window.outerWidth` vs `window.innerWidth`).
5. **React:**
   - Add `<ViewModeMenu>` component (popover with two items).
   - Read current view mode on mount via `pageHandler.getViewMode()`.
   - Render the trigger as `IconButton symbol="layoutToggle"` (use the
     existing `panelSpark`-style icon if no better one is in the set —
     OR add a new symbol entry to `icons.ts` named `viewModeToggle`
     pointing at an SF Symbol like `square.split.bottomrightquarter`).
   - On selection: `pageHandler.setViewMode(...)`.
6. **State on launch:** when the page handler initializes, if
   `kAiViewMode == "floating"` and the host context is the side panel
   (i.e. user closed the popup but pref still says floating), proceed
   with sidebar — i.e., self-correct the pref to `"sidebar"`. Avoids
   reopen loops.

**Verification:**
- Build passes (mojom regen).
- Live:
  - Side panel mode: select Floating → popup window opens with
    chrome://maho-ai content; side panel closes; same conversation
    state if persisted (Phase 3 of persistence plan handles this).
  - Floating mode: select Sidebar → popup closes; side panel re-opens.
  - Restart Maho with `kAiViewMode == "floating"` set → if popup is
    not auto-restored, the pref self-corrects to `"sidebar"` and panel
    opens as side panel (no infinite reopen loop).
- Pref persistence: change view mode, restart Maho, mode persists.

**Caveats:**
- Popup window decorations (title bar, close button) are
  Chromium-managed; the topbar's own X button (Phase B) coexists with
  Chromium's window-frame close. The X close should call `ClosePanel`
  which in popup mode = close the popup window (browser-side
  detection: `if (host == popup) Browser::window()->Close()`). In
  sidebar mode = close the side panel.
- If the popup window's bounds are tiny by default, the panel will
  feel cramped. Pick a sensible default and let user resize.
- macOS treats popup windows as standard windows; window menu /
  menubar reflects this. Acceptable.

**Rollback:** revert all changes. Pref left at "floating" auto-corrects
on next launch (covered by step 6).

## 6. Risks and mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Phase D infinite reopen loop (popup → close → re-open via pref) | Medium | High | Step 6: self-correct pref on launch when host context is sidebar. |
| `chrome://maho-ai/` rendered in popup looks broken (sidebar-specific CSS) | Medium | Medium | Pre-test in popup; if broken, gate sidebar-only CSS on a query string or media-query proxy. |
| `OpenSettings()` opens settings in a side panel instead of a tab | Low | Low | Use `WindowOpenDisposition::SINGLETON_TAB` explicitly, not `NEW_POPUP`. |
| ViewMode mojom enum collides with future enums | Low | Low | Place in same `module maho_ai.mojom`, additive. |
| Phase D's popup window handling differs from Maho's main window construction | Medium | Medium | Use `Browser::OpenURL` not direct `Browser::Create` — let Chromium pick the right Browser instance for popups. |
| Backwards compat: existing users have no `kAiViewMode` pref | None | None | Default `"sidebar"` matches current behavior. |

## 7. Verification strategy

1. **Phase A standalone:** screenshot before/after, full-width topbar
   visible, no overlap with thread.
2. **Phase B unit:** click X → side panel closes (DOM probe via CDP
   confirms host detached).
3. **Phase C unit:** click gear → chrome://maho-settings tab opens.
4. **Phase D integration:**
   - Toggle to Floating: popup window appears; side panel closes.
   - Toggle to Sidebar: popup closes; side panel re-opens.
   - Restart Maho mid-floating: see step 6 fallback.
   - Stress: rapid toggles don't leak windows.
5. **Visual regression:** Dia-side-by-side comparison of empty state.
   Topbar geometry should match Dia within ~2px.

## 8. Recommended execution order

```
Phase A — CSS layout                 ← lowest risk, single file
Phase B — Close panel button         ← uses existing mojom
Phase C — Personalization            ← adds 1 mojom method
Phase D — View mode chooser          ← largest scope; own commit
```

A+B+C can ship as one commit (small, related, testable together). D
needs its own commit and likely its own Oracle review pass before
shipping.

## 9. Honesty statement

Phase D is non-trivial. Detaching a WebUI side panel into a popup
window is doable in Chromium but introduces a second host context
(popup vs side-panel) that all subsequent UX must account for —
specifically, the X close button has to behave correctly in both, and
the page handler's `Browser*` reference must remain valid across the
transition. If during implementation we find that the popup host
exposes Chromium APIs that the side-panel host does not (or vice
versa), Phase D may need to grow another sub-step to abstract a
"PanelHost" interface. The plan does not pre-commit to that
abstraction.
