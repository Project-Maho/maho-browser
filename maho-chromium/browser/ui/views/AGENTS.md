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
├── tabs/            # Tab strip customization
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

## ANTI-PATTERNS
- No WebUI dialogs for non-WebUI features — command palette stays native Views
- **Theme picker is permanently `chrome://maho-space-create`** — do NOT add a second native Views theme picker (see ADR at `.sisyphus/plans/theme-picker-architecture-decision.md`)
- No hand-editing files under `maho_lucide_icons/` — they are auto-generated
- No UI state in `split_view/` that belongs in `maho-core` split view manager

## NOTES
- Design reference: `maho/reference_screenshots/` — always check before claiming visual parity
- `DESIGN.md` at directory root has architectural notes
- Sidebar tests: `maho_sidebar_*_test.cc` (interactive) and `maho_sidebar_*_unittest.cc` (unit)
- **macOS inactive-window text color (`kInactiveWidgetControlsAppearDisabled`)**: on macOS Chromium sets `kInactiveWidgetControlsAppearDisabled = true`, so `views::Button::GetVisualState()` returns `STATE_DISABLED` whenever `!GetWidget()->ShouldPaintAsActive()` (i.e. the window is not key), regardless of the button's real enabled state. Any sidebar `LabelButton` whose `STATE_DISABLED` text color is not bound to the resolved space palette will therefore change color when the window loses focus, while the palette-painted background stays put — looking like "only the text color flips on blur" (worst on themes where the untethered disabled color is low-contrast against the space background, e.g. bright/saturated themes). Fixed for tab titles and folder names in `maho_sidebar_tab_list_view.cc` by also calling `SetTextColor(views::Button::STATE_DISABLED, <resolved palette color>)` in the palette-apply paths. KNOWN RESIDUAL (not yet fixed): utility buttons that set color only via `SetEnabledTextColors(...)` (New Tab / Tidy / Clear / action buttons in the same file) do NOT set a disabled color, so they can still dim on inactive; fix identically by adding an explicit `STATE_DISABLED` color if it ever matters. Do NOT chase this as a ColorProvider/vibrancy bug — the ColorProvider color mode does not flip on window key changes, and the window `NSVisualEffectView` is pinned `NSVisualEffectStateActive`.
