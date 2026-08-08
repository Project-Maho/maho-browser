# UI DOMAIN GUIDE

## OVERVIEW
Maho's browser-facing UI layer, spanning native Views, Mojo-backed WebUI pages, shared visual state, and browser chrome integrations.

## STRUCTURE
```
ui/
├── views/          # Native Chromium Views: sidebar, command overlay, split view
├── webui/          # C++ WebUI controllers, Mojo handlers, dialog hosts
├── theme/          # Maho color mixer, color IDs, and space theme state
├── context_menu/   # Menus for tabs, spaces, folders, favorites, and address bar
├── find_bar/       # Find-in-page appearance and integration
├── reader/         # Reader-mode presentation and theming
├── downloads/      # Download UI customization
├── permissions/    # Permission prompt customization
├── tab_preview/    # Tab preview controller and surfaces
├── notifications/  # Browser notification overlay and native share support
└── site_control/   # Page-info and site-control UI
```

## WHERE TO LOOK
| Task | Location |
|------|----------|
| Native sidebar, spaces, tabs, favorites, drag and drop | `views/sidebar/` |
| Command palette and search result presentation | `views/command/` |
| Native dialogs, split view, and browser chrome controls | `views/` |
| A browser-owned WebUI controller or Mojo handler | `webui/maho_{feature}/` |
| Frontend source for a WebUI surface | `../../resources/maho_{feature}/` |
| Space colors and resolved theme state | `theme/` |
| Context-sensitive browser menus | `context_menu/` |
| Boost appearance and behavior | `webui/maho_boost/` plus `../../resources/maho_boost/` |
| Theme picker and space creation dialog | `webui/maho_space_create/` plus its dialog host in `views/` |
| Tab previews, notifications, and page information | `tab_preview/`, `notifications/`, `site_control/` |

## CONVENTIONS
- Start at the layer that owns the interaction. Keep browser window behavior in `views/`, and page-contained flows in `webui/`.
- Treat a WebUI surface as a vertical slice: C++ controller, handler, Mojo contract, resource bundle, and tests move together.
- Make native Views react to `OnThemeChanged()` and source colors from the UI theme layer rather than caching hard-coded colors.
- Keep UI controller code focused on lifecycle, view state, and wiring. Place data operations in the owning browser helper or model.
- Model transient UI safely: unregister observers, invalidate callbacks with weak pointers, and close dialogs through their owning controller.
- Add test-only accessors only when they assert observable state or layout behavior, not implementation details.
- Follow the more specific guidance in `views/AGENTS.md` and `webui/AGENTS.md` when working below either directory.

## ANTI-PATTERNS
- Do not duplicate a browser interaction in both native Views and a WebUI surface without a clear ownership boundary.
- Do not make a WebUI handler the source of truth for persistent browser state.
- Do not bypass theme and color infrastructure with literal product colors in View code.
- Do not leave widget observers, async callbacks, or native share delegates alive after their owner closes.
