# PROJECT KNOWLEDGE BASE — browser/ui/views

**Generated:** 2026-07-14

## OVERVIEW
Native Views layer: all Chromium C++ UI widgets. Sidebar is the largest subsystem (~25 files). 193 CC/H files across 14 subdirectories.

## STRUCTURE
```
browser/ui/views/
├── sidebar/         # Biggest subsystem: container, tabs, favorites, DnD, search, spaces
├── command/         # Command palette (native Views; no WebUI counterpart)
├── split_view/      # Split-screen tab layout (two WebContents side-by-side)
├── spaces_overlay/  # Arc-style Spaces overlay (archive/spaces library)
├── space_config/    # Space configuration native dialog
├── space_create/    # Space creation entry (launches chrome://maho-space-create WebUI dialog)
├── maho_mini/      # Maho Mini preview popover (link preview)
├── boost/           # Boost badge/UI in toolbar
├── shields/         # Ad/tracker shield status indicator
├── side_panel/      # Side panel host integration
├── location_bar/    # Address bar customization
├── library_overlay/ # Overlay presentation for library and downloads
├── frame/           # Tab controlled banner and frame overlay hosts
├── passwords/       # Native password save and update prompt views
├── welcome/         # Native welcome window host
├── maho_lucide_icons/ # aggregate_vector_icons GN target (auto-generated from manifest.json)
├── importer/        # (See browser/importer/AGENTS.md for importer details)
└── maho_space_delete_confirmation_dialog.cc/h
```

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Sidebar (container, DnD, tabs) | `sidebar/` | ~25 files; tests: `maho_sidebar_*_test.cc` |
| Command palette | `command/` | Native Views only; keyboard-driven |
| Split view | `split_view/` | `maho_split_view_manager.cc` coordinates two WebContents |
| Spaces overlay (Arc-style library) | `spaces_overlay/` | Consult `maho/reference_screenshots/arc-library-space.jpeg` |
| Theme picker entry | `space_create/` → `MahoSpaceThemePickerDialog::Open` | Hosts `chrome://maho-space-create` WebUI |
| Link preview | `maho_mini/` | |
| Lucide icons (C++) | `maho_lucide_icons/` | `manifest.json` → `generate.py --write`. NEVER hand-edit `.icon` files |

## CONVENTIONS
- `DCHECK_CALLED_ON_VALID_SEQUENCE` on all UI-thread methods
- Namespace-local helpers preferred over member functions for pure transforms
- Views subsystems have their own `BUILD.gn`; each subsystem is a GN target
- Tests co-located as `maho_*_test.cc` / `maho_*_unittest.cc`
- Icon update flow: bump `lucide_version` in `manifest.json` → `python3 build/scripts/lucide_icons/generate.py --write`
- **Sidebar Palette Authority**: Sidebar surfaces (sidebar body, library overlay, Spaces overlay) source surface, text, and glyph colors exclusively from `MahoSidebarPalette`. Direct `kMahoColor*` ColorId references on those surfaces are prohibited and enforced by `maho-chromium/build/scripts/check_sidebar_theme_color_ledger.py`.
- **Contrast Guard**: Text must meet >= 4.5:1 contrast against resolved backgrounds; functional glyphs and boundaries >= 3:1. Forced-colors / high-contrast system modes take precedence over space custom themes.
- **Incognito & OTR Isolation**: Private / OTR windows must never access regular space bridge state, persist sidebar preferences, or cross-enumerate regular profile tabs and downloads.

## ANTI-PATTERNS
- No WebUI dialogs for non-WebUI features — command palette stays native Views
- **Theme picker is permanently `chrome://maho-space-create`** — do NOT add a second native Views theme picker (see ADR at `.sisyphus/plans/theme-picker-architecture-decision.md`)
- No hand-editing files under `maho_lucide_icons/` — they are auto-generated
- No UI state in `split_view/` that belongs in `maho-core` split view manager
- No direct `ColorId` referencing on sidebar-palette surfaces — use resolved `MahoSidebarPalette`
- No accessing `MahoCore` or persisting UI state in Incognito / OTR sessions
- No recursive/re-entrant `ReconcileTabList` invocations during layout or scroll mutations

## NOTES
- **Shortcut triple-sync contract**: adding/changing a key binding touches three synchronized lists — Rust registry (`shortcut_manager.rs`), C++ dispatch chain (`maho_command_action_handler.cc`), C++ key codec (`maho_shortcut_interceptor.cc`). Follow the checklist in `docs/shortcut-registry-contract.md`. Enforced by `maho_shortcut_contract_unittest.cc` (C++) and `contract_tests` in `shortcut_manager.rs` (Rust). An unhandled registry action is silently swallowed and also suppresses Chromium's native fallback for that key.
- Design reference: `maho/reference_screenshots/` — always check before claiming visual parity
- `DESIGN.md` at directory root has architectural notes
- Sidebar tests: `maho_sidebar_*_test.cc` (interactive) and `maho_sidebar_*_unittest.cc` (unit)
- **macOS inactive-window text color (`kInactiveWidgetControlsAppearDisabled`)**: on macOS Chromium sets `kInactiveWidgetControlsAppearDisabled = true`, so `views::Button::GetVisualState()` returns `STATE_DISABLED` whenever `!GetWidget()->ShouldPaintAsActive()` (i.e. the window is not key), regardless of the button's real enabled state. Any sidebar `LabelButton` whose `STATE_DISABLED` text color is not bound to the resolved space palette will therefore change color when the window loses focus, while the palette-painted background stays put — looking like "only the text color flips on blur" (worst on themes where the untethered disabled color is low-contrast against the space background, e.g. bright/saturated themes). Fixed for tab titles and folder names in `maho_sidebar_tab_list_view.cc` by also calling `SetTextColor(views::Button::STATE_DISABLED, <resolved palette color>)` in the palette-apply paths. The same binding covers the New Tab / Tidy / Clear action rows (`ApplyActionRowPalette()`) and every other palette-bound sidebar button (footer, top bar, search, favorites, library, media, now playing); a new sidebar button must set an explicit `STATE_DISABLED` text/image color from the palette too. Do NOT chase this as a ColorProvider/vibrancy bug — the ColorProvider color mode does not flip on window key changes, and the window `NSVisualEffectView` is pinned `NSVisualEffectStateActive`.
