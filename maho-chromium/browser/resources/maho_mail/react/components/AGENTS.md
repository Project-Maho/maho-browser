# COMPONENTS KNOWLEDGE BASE

**Scope:** `src/components/`

## OVERVIEW
Feature-oriented UI tree. Keep each folder narrow and keep reusable primitives separate from feature screens.

## STRUCTURE
```
components/
├── account/
├── common/
├── compose/
├── layout/
├── settings/
└── ui/
```

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Reusable primitives | `ui/` | Button/Input/Toast/Dialog/Skeleton, plus barrel export |
| App shell layout | `layout/` | Sidebar, list, content panes |
| Compose flow | `compose/` | Modal + rich text editor |
| Account screens | `account/` | Add account flow |
| Settings | `settings/` | Preferences panel |
| Shared empty states | `common/` | Cross-feature generic UI |

## CONVENTIONS
- `ui/` is the only place for base primitives.
- Feature folders may have `__tests__/` siblings.
- Prefer small, focused components over large multipurpose ones.
- Keep component-specific logic local; move orchestration to hooks when it grows.

## ANTI-PATTERNS
- No importing feature-specific UI into `ui/`.
- No cyclic dependencies between feature folders.
- No duplicated button/input/dialog variants when a primitive already exists.
- No test-only helpers mixed into production component files.

## NOTES
- `ui/index.ts` is the stable export surface for primitives.
- Layout components depend heavily on `hooks/` and `api/`-backed state from `App.tsx`.
