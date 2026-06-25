# ADR — Theme Picker Architecture (Permanent)

**Date**: 2026-05-22
**Status**: ✅ Decided — Permanent. Supersedes all prior conflicting plans.
**Decided by**: User explicit instruction.

---

## Decision

The Maho theme picker UI is **permanently** implemented as a Chromium **WebUI** at `chrome://maho-space-create`, hosted by a Views dialog widget (`MahoSpaceThemePickerDialog`).

### Production Path (canonical, do not change)

```
Sidebar footer "+" → New Space
  └─ MahoSidebarView::ShowCreateSpace()
      └─ MahoSidebarCreateSpaceView (native form)
          └─ "Choose a Theme" row clicked
              └─ MahoSidebarCreateSpaceView::OnThemePressed()
                  [maho_sidebar_create_space_view.cc:968]
                  └─ MahoSpaceThemePickerDialog::Open(browser, initial_json, callback)
                      [maho_space_theme_picker_dialog.cc]
                      └─ Hosts WebUI in views::Widget dialog
                          └─ chrome://maho-space-create
                              └─ react/app.tsx (the React picker)
```

### Implementation Surface

- **WebUI URL**: `chrome://maho-space-create`
- **WebUI host class**: `MahoSpaceCreateUI` (`browser/ui/webui/maho_space_create/maho_space_create_ui.cc`)
- **Dialog wrapper**: `MahoSpaceThemePickerDialog` (`browser/ui/views/space_create/maho_space_theme_picker_dialog.cc`)
- **Page handler**: `MahoSpaceCreatePageHandler`
- **Mojo IDL**: `browser/ui/webui/maho_space_create/maho_space_create.mojom`
- **React app**: `browser/resources/maho_space_create/react/`
- **CSS**: `browser/resources/maho_space_create/space_create.css`
- **Standalone dev harness**: `browser/resources/maho_space_create/standalone/`

---

## Forbidden

The following are **explicitly forbidden** and must not be revisited:

1. ❌ **Migration to `maho_space_config`** — `maho_space_config` is for editing existing space settings only. The theme picker must not be merged into it. Any plan or comment proposing this migration is superseded by this ADR.

2. ❌ **Native Views theme picker** — The native C++ Views theme picker (lines ~200-1232 of `maho_sidebar_create_space_view.cc` historically) is permanently deleted. Do not resurrect.

3. ❌ **Second production WebUI surface for theme picking** — Only `chrome://maho-space-create` hosts the picker. No alternative URL.

4. ❌ **Mojom schema changes for `theme_json`** — `theme_json` remains an opaque `string`. Schema lives in TypeScript adapters. Do not add typed fields to the Mojom interface.

---

## Context

### Why this decision

- Live production code already routes through `MahoSpaceThemePickerDialog::Open` → `chrome://maho-space-create`. The WebUI exists, is registered, has a Page Handler, and is functional.
- Multiple prior plans (`theme-parity-shared-file-plan.md`, `theme-picker-webui-migration-plan.md`, etc.) proposed migrating to `maho_space_config` for "shared production surface." This migration was **never implemented in code** and creates two-source-of-truth ambiguity. The user explicitly directs that we will not pursue this migration.
- The native Views picker was deleted in earlier sessions because it was unmaintained and visually drifted from reference.

### What this supersedes

The following plans contain proposals that conflict with this ADR. Their contents are reference material only; their proposed direction is **rejected**:

- `theme-parity-shared-file-plan.md` — proposed migrating create-mode into `maho_space_config`
- `theme-picker-webui-migration-plan.md` — proposed sub-dialog migration boundary at `OnThemePressed()` (the boundary is correct; the destination URL was wrong)
- `space-new-revert-to-webui-plan.md` — proposed full architectural revert
- `theme-parity-reference-index.md` — used as inventory only; mapping to `maho_space_config` is rejected

The following plans remain **valid reference**:

- `theme-picker-completion-plan.md` — Phases 0-4 already complete on disk; this ADR formalizes the architecture they implement
- `native-theme-chooser-visual-parity-plan-v2.md` — visual GT specification (still authoritative)
- `space-theme-one-pass-parallel-plan.md` — JSON canonicalization, bridge unification (still authoritative)

---

## Consequences

### Stays in `maho_space_create`
- React picker UI
- Theme JSON serialization/deserialization
- Preset table
- All visual parity work
- All wire-format adapter work

### Stays in `maho_space_config`
- Space settings editor (rename, icon change, profile management, theme **edit** for existing space)
- It is a **different** WebUI for a different lifecycle (post-create edit)

### Future changes
- All theme picker UX improvements happen in `maho_space_create/react/`
- All wire-format changes happen in `maho_space_create/react/types.ts` (TS adapters); Mojom remains opaque string
- Native sidebar form changes happen in `maho_sidebar_create_space_view.cc` only for the form itself, not for the picker

### Cross-surface theme reuse
- If `maho_space_config` needs to reuse theme picker UI, it imports the React component from `maho_space_create/react/` as a library, **not** by absorbing the surface
- This keeps `chrome://maho-space-create` as the single picker production URL

---

## References

- Master implementation plan: `.sisyphus/plans/theme-picker-implementation-plan-2026-05-22.md`
- Visual GT: `maho/reference_screenshots/create-theme.png`
- AGENTS.md surface table: `maho-chromium/AGENTS.md` § WEBUI SURFACES
