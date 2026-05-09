# Command Palette Feature Inventory

**Reference set:** Arc, Zen, Nook  
**Date:** 2026-05-06

## Purpose

This document lists the **union of capabilities** a modern browser command palette / floating URL bar should support when using **Arc**, **Zen**, and **Nook** as product references.

This is a **feature inventory**, not a pixel-perfect visual spec.

## Interpretation rules

1. **Arc** and **Nook** explicitly document command-palette behavior.
2. **Zen** does **not** document an Arc-style “do anything a mouse can do” command palette. Zen is still useful as a reference for:
   - floating URL/search entry,
   - search engine shortcuts,
   - suggestion-source controls,
   - recent-search behavior,
   - shortcut customization.
3. If the source documents conflict, this file preserves the conflict instead of hiding it.

## Source set

### Arc

- `https://start.arc.net/command-bar-actions`
- `https://start.arc.net/master-multitasking`
- `https://resources.arc.net/hc/en-us/articles/20595231349911-Keyboard-Shortcuts`
- `https://resources.arc.net/hc/en-us/articles/20855018192791-Site-Search-Directly-Search-any-Website`

### Zen

- `https://docs.zen-browser.app/user-manual/urlbar`
- `https://docs.zen-browser.app/user-manual/shortcuts`
- `https://docs.zen-browser.app/user-manual/split-view`

### Nook

- `https://nook-browser-nook.mintlify.app/features/command-palette`
- `https://nook-browser-nook.mintlify.app/features/sidebar-navigation`
- `https://nook-browser-nook.mintlify.app/features/tabs-and-spaces`
- `https://nook-browser-nook.mintlify.app/customization/keyboard-shortcuts`

## Reference confidence

| Reference | What it contributes well | Confidence |
|---|---|---|
| Arc | Breadth of command/action surface, spaces/folders, extension commands, site search, command bar as a primary browser control center | High |
| Zen | Floating URL/search bar behavior, search shortcuts, suggestion controls, recent searches, shortcut customization | Medium |
| Nook | Explicit command palette modes, sidebar-integrated invocation, profile/incognito scoping, multi-window tab search, site-search UX | Medium |

## Canonical feature inventory

### 1. Entry points and modes

- **P0** — Support **new-tab draft mode** from `Cmd/Ctrl+T` or equivalent “New Tab” entrypoint. Submitting a URL or query must create a **new tab**, not replace the current page.  
  **References:** Arc, Nook, Zen New Tab floating URL bar.
- **P0** — Support **navigate-current-tab mode** from `Cmd/Ctrl+L` or equivalent “focus/open location” action. Submitting a URL or query must reuse the **current tab**.  
  **References:** Arc keyboard shortcuts, Nook command palette, Zen `Open Location`.
- **P0** — Support opening from **URL bar click** / sidebar URL field click.  
  **References:** Nook explicit, Zen explicit.
- **P1** — Support opening from other product entrypoints such as **sidebar context menu → New Tab** or equivalent shell UI affordances.  
  **References:** Nook explicit.
- **P1** — Allow a product to expose a **separate general actions palette** in addition to navigation entrypoints if desired.  
  **References:** Nook shortcut docs list `Cmd+Shift+P` for “Open Command Palette”, which conflicts with Nook’s feature page centered on `Cmd+T` / `Cmd+L`.
- **P1** — Allow the floating surface behavior to be configurable, e.g.:
  - always floating,
  - floating only when typing / from keyboard,
  - attached / normal.  
  **Reference:** Zen explicit.
- **P0** — Close cleanly with **Escape** and return focus predictably.
- **P1** — Preserve distinct behavior for:
  - “open something new”,
  - “change current page”,
  - “invoke browser command/action”.

### 2. Unified query handling

- **P0** — Treat the field as a **unified input surface** for:
  - URL navigation,
  - web search,
  - tab switching,
  - history navigation,
  - action/command execution.  
  **References:** Arc, Nook, Zen URL/search model.
- **P0** — Perform **smart URL detection** for domains vs plain-text search queries.  
  **Reference:** Nook explicit.
- **P0** — Support direct entry of partial/bare domains and resolve them intelligently.  
  **Reference:** Nook explicit (`github` example).
- **P1** — Persist unfinished query text when the floating bar is dismissed without navigation, until another navigation/refresh clears it.  
  **Reference:** Zen explicit.
- **P0** — Update results **in real time** while typing.  
  **Reference:** Nook explicit.

### 3. Suggestion sources and search scope

- **P0** — Search **open tabs** and switch directly to a matching tab.  
  **References:** Arc, Nook, Zen tab/search ecosystem.
- **P0** — Search **browsing history** and navigate directly to history matches.  
  **References:** Arc, Nook, Zen search settings.
- **P1** — Search **bookmarks / pinned items / favorites** as first-class suggestions.  
  **References:** Zen search-source controls, Arc organization model.
- **P1** — Search **spaces/workspaces** by name and jump directly to them.  
  **References:** Arc “Focus On [Space Name]”, Nook explicit, Zen workspace model.
- **P1** — Search **folders** by name and surface nested destinations that are not immediately visible in the sidebar.  
  **Reference:** Arc explicit (“Finding a folder tucked away somewhere”).
- **P1** — Search **tabs across multiple windows**, not only the current window.  
  **Reference:** Nook explicit.
- **P1** — Support suggestion-source toggles for at least:
  - open tabs,
  - history,
  - bookmarks,
  - clipboard,
  - search engines,
  - recent searches.  
  **References:** Zen explicit, Nook settings mention enabling/disabling suggestion types.
- **P1** — Support **recent searches** as a suggestion source.  
  **Reference:** Zen explicit.
- **P0** — Scope suggestions by the **current profile / privacy context**.  
  **References:** Nook explicit profile filtering, Zen regular/private engine configuration.

### 4. Site search and search-engine integration

- **P0** — Support **custom site-search shortcuts / keywords** inside the palette.  
  **References:** Arc explicit, Nook explicit, Zen search shortcuts.
- **P0** — Allow adding and editing custom search engines with at least:
  - display name,
  - keyword/shortcut,
  - URL template using `%s`.  
  **References:** Arc, Zen, Nook.
- **P1** — Support optional advanced engine fields such as:
  - suggestions URL,
  - POST data.  
  **Reference:** Zen explicit.
- **P0** — Allow in-palette activation of site-search mode from a typed keyword plus confirmation key.  
  **References:** Arc uses `Tab`, Nook uses `Tab`, Zen uses `Space`.
- **P1** — Display a visible **site-search badge / mode chip** once site search is active.  
  **Reference:** Nook explicit.
- **P1** — Support different default search engines for **regular** vs **private/incognito** browsing.  
  **Reference:** Zen explicit.
- **P1** — Allow search-engine ordering / priority controls for URL-bar suggestions.  
  **Reference:** Zen explicit.

### 5. Result rows and visual feedback

- **P0** — Render distinct result rows for at least:
  - URL/navigation,
  - web search,
  - open tab,
  - history,
  - action/command.  
  **References:** Nook explicit, Arc command/action model.
- **P0** — Show **title + secondary metadata** (URL, subtitle, destination context, or command description).
- **P0** — Show a **favicon** or contextual leading icon for each result.  
  **References:** Nook explicit, Arc visual model.
- **P0** — Highlight the currently selected result with a strong, space-aware/accent-aware selection treatment.  
  **Reference:** Nook explicit.
- **P1** — Adapt the leading iconography to query interpretation state, e.g.:
  - globe for direct URL,
  - magnifier for search,
  - site-search badge/icon for scoped search.  
  **Reference:** Nook explicit.
- **P1** — Use a floating, centered, top-of-window overlay or floating URL bar with responsive width.  
  **References:** Arc, Zen, Nook.
- **P1** — Use smooth state transitions / animations when opening, closing, or changing result states.  
  **References:** Nook explicit, Arc polished command bar behavior.

### 6. Keyboard interaction model

- **P0** — `Up/Down` moves the selected suggestion.
- **P0** — `Enter` activates the selected suggestion.
- **P0** — `Escape` closes the palette or exits the active submode.
- **P0** — `Tab` (or product-chosen equivalent) enters site-search/search-engine mode.
- **P1** — `Delete/Backspace` exits site-search mode when the field is empty.  
  **Reference:** Nook explicit.
- **P1** — Keyboard shortcuts for palette access and adjacent browser actions must be **customizable/remappable**.  
  **References:** Arc, Zen, Nook explicit.
- **P1** — Shortcut systems should detect and report **conflicts**:
  - browser shortcut vs browser shortcut,
  - browser shortcut vs website shortcut where applicable.  
  **References:** Nook explicit, Zen explicit.

### 7. Command/action surface

- **P0** — Support browser navigation actions such as:
  - back,
  - forward,
  - reload,
  - find in page,
  - copy current URL,
  - change current tab URL.  
  **References:** Arc, Zen, Nook.
- **P0** — Support tab-lifecycle actions such as:
  - new tab,
  - close tab,
  - reopen last closed tab,
  - duplicate tab,
  - pin/unpin current tab.  
  **References:** Arc, Zen, Nook.
- **P1** — Support richer tab actions where the product model includes them, such as:
  - archive tab,
  - favorite tab,
  - reset pinned tab,
  - replace pin with current page,
  - reveal tab in sidebar.  
  **Reference:** Arc explicit; Zen reset pinned tab.
- **P0** — Support space/workspace actions such as:
  - create space/workspace,
  - switch to named space/workspace,
  - move to next/previous space/workspace.  
  **References:** Arc, Zen, Nook.
- **P1** — Support folder actions such as:
  - create folder,
  - pin/move tab to folder,
  - surface folder by search.  
  **References:** Arc explicit, Nook folder model.
- **P1** — Support split-view actions such as:
  - add split,
  - remove split,
  - expand current split,
  - switch split layout/focus.  
  **References:** Arc explicit, Zen explicit.
- **P1** — Support browser-tool actions such as:
  - open downloads,
  - open history,
  - open archive/library,
  - open developer tools,
  - enter full screen,
  - print/save page,
  - zoom in/out/reset.  
  **References:** Arc, Zen, Nook.
- **P1** — Support settings and extension actions such as:
  - open preferences/settings,
  - manage extensions,
  - add/open an extension by name.  
  **References:** Arc explicit, Nook settings/shortcuts.
- **P2** — Support productivity/integration actions where the product has those capabilities, such as:
  - new note,
  - new canvas/easel,
  - page capture,
  - join call,
  - create new external docs/pages in integrated tools.  
  **Reference:** Arc explicit.

### 8. Personalization and context awareness

- **P0** — Respect the **current space/workspace** when deciding where a newly submitted item is created.  
  **References:** Arc spaces model, Nook explicit current-space behavior.
- **P0** — Filter tab/history suggestions by the **current profile** in multi-profile setups.  
  **Reference:** Nook explicit.
- **P0** — Change behavior for **private/incognito** windows, e.g.:
  - no saved history suggestions,
  - only tabs from the current incognito/private session.  
  **Reference:** Nook explicit.
- **P1** — Use space/workspace accent colors to tint selection or chrome where the product visual model supports it.  
  **Reference:** Nook explicit.
- **P1** — Preserve compatibility with keyboard-first browsing flows instead of assuming mouse-first fallback.  
  **References:** Arc, Zen, Nook.

## Reference-specific stretch capabilities

These are real reference features, but not every browser needs all of them on day one.

- **Arc stretch** — The command bar should be able to surface nearly **any browser action a mouse can do**.  
  **Reference:** Arc `Master your multitasking`.
- **Arc stretch** — Surface extension names directly in the palette and let users launch/manage them from search.  
  **Reference:** Arc `Command Bar Actions`.
- **Arc stretch** — Surface space/folder organization commands deeply enough that the palette becomes the fastest way to reorganize browser state.  
  **Reference:** Arc `Command Bar Actions`.
- **Zen stretch** — Preserve query text between openings/dismissals when the user has not navigated yet.  
  **Reference:** Zen URL Bar docs.
- **Zen stretch** — Expose fine-grained search-source priority and search-engine configuration directly in settings.  
  **Reference:** Zen Search / URL bar docs.
- **Nook stretch** — Explicitly search tabs across **all windows** from the same palette surface.  
  **Reference:** Nook command palette docs.
- **Nook stretch** — Make the sidebar URL field a clean, click-only launcher for the command palette rather than a separate inline editor.  
  **Reference:** Nook sidebar navigation docs.

## Known source conflicts / open questions

1. **Nook shortcut conflict**  
   Nook’s feature docs say the command palette is opened with `Cmd+T` (new tab mode) or `Cmd+L` (navigate mode), while Nook’s keyboard-shortcuts page also lists `Cmd+Shift+P` for “Open Command Palette”. Treat this as an unresolved product/doc conflict until verified in the running product.

2. **Zen scope mismatch**  
   Zen does not document a separate Arc-style command palette. Use Zen as a reference for **floating URL/search behavior**, **search shortcuts**, **suggestion controls**, and **shortcut customization** — not as proof that Zen ships the same action-command surface as Arc.

3. **Action-surface breadth**  
   Arc is the strongest reference for “browser-wide actions discoverable from one palette”. If a product wants broad command coverage, Arc should be treated as the primary reference for that dimension.

## Practical implementation priority

### P0 baseline

A modern browser command palette is not credible without these:

1. New-tab mode vs current-tab navigate mode.
2. Unified URL/search/tab/history input.
3. Smart URL detection.
4. Open-tab switching.
5. History results.
6. Site search / search shortcuts.
7. Keyboard selection + activation.
8. Profile/private-window aware suggestion scoping.
9. Basic browser actions.

### P1 expansion

1. Space/workspace/folder search and commands.
2. Split-view commands.
3. Settings and extension actions.
4. Suggestion-source toggles.
5. Multi-window tab search.
6. Accent-aware visual feedback.
7. Floating-behavior configuration.

### P2 differentiation

1. Broad Arc-style “anything a mouse can do” action surface.
2. Productivity/integration commands.
3. Persistent query memory.
4. Advanced engine configuration fields.

## Bottom line

If this product wants command-palette parity with the strongest ideas across Arc, Zen, and Nook, the command palette should be treated as **three things at once**:

1. a **floating URL/navigation surface**,
2. a **unified search and switcher** for tabs/history/spaces/folders,
3. a **browser-wide command launcher** for actions, organization, settings, and tools.
