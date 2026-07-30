# UI PRIMITIVES KNOWLEDGE BASE

**Scope:** `src/components/ui/`

## OVERVIEW
Shared visual primitives and app-wide interaction widgets. This folder mixes custom wrappers with a small set of lowercase shadcn-style files; keep it primitive-focused and do not move feature UI in here.

## STRUCTURE
```
ui/
├── PascalCase wrappers  # Button, Input, Toast, Tooltip, ConfirmDialog, ErrorBoundary, etc.
├── lowercase shadcn    # alert-dialog, checkbox, command, context-menu, dialog, drawer, label, popover, sheet, slider
├── index.ts             # Public barrel for the stable app-facing primitives
└── utils.ts             # `cn()` helper re-export (clsx + tailwind-merge)
```

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Stable exports | `index.ts` | Preferred import surface for app code |
| Class merge helper | `utils.ts` | Shared `cn()` helper |
| App-wide controls | `Button.tsx`, `Input.tsx`, `Textarea.tsx`, `Select.tsx`, `Switch.tsx`, `Badge.tsx` | Base primitives used across features |
| Global overlays | `Toast.tsx`, `ConfirmDialog.tsx`, `Tooltip.tsx`, `ErrorBoundary.tsx`, `ShortcutCheatsheet.tsx`, `CommandPalette.tsx` | App shell interaction pieces |
| Radix-backed internals | `alert-dialog.tsx`, `command.tsx`, `context-menu.tsx`, `dialog.tsx`, `drawer.tsx`, `popover.tsx`, `sheet.tsx`, `slider.tsx`, `checkbox.tsx`, `label.tsx` | Lower-level building blocks |

## CONVENTIONS
- Keep this folder primitive-only: buttons, dialogs, palettes, overlays, and very small app-wide wrappers.
- Prefer exporting from `index.ts` when the component is part of the stable app surface.
- Use `PascalCase` for app-owned wrappers and lowercase for direct shadcn-style primitives.
- Keep feature-specific behavior in `components/<feature>/`, not in `ui/`.

## ANTI-PATTERNS
- No feature screens or business logic here.
- No duplicate primitive variants when an existing component already covers the use case.
- No importing `ui/` internals back into feature code when a stable export already exists.

## NOTES
- The naming split is intentional and currently mixed; do not “normalize” it without a repo-wide cleanup.
- `index.ts` does not export every file in the directory; some lowercase primitives are intentionally imported directly.
