# PROJECT KNOWLEDGE BASE — browser/resources

**Generated:** 2026-07-14

## OVERVIEW
TypeScript/React frontends for WebUI surfaces. Each subdirectory is a self-contained SPA compiled via GN + esbuild. Uses **shadcn + Tailwind CSS v4** with shared primitives in `maho_common/`.

## STRUCTURE
```
browser/resources/
├── maho_common/              # Shared library — used by ALL feature apps
│   ├── react/
│   │   ├── ui/               # shadcn-registry primitives (components.json declares them)
│   │   └── theme/
│   │       ├── tokens.css    # CSS custom properties / Tailwind theme vars
│   │       └── apply_theme   # Writes data-theme to <html>; call on init
│   └── components.json       # shadcn registry spec for shared UI
├── dom_adapters/             # Page DOM surface and React Fiber inspection adapters
├── maho_ai/                  # chrome://maho-ai frontend
├── maho_boost/               # chrome://maho-boost frontend
├── maho_boost_content_script/ # Boost content script (injected into pages)
├── maho_mail/                # chrome://maho-mail frontend
├── maho_routines/            # chrome://maho-routines frontend
├── maho_settings/            # chrome://maho-settings frontend
├── maho_space_config/        # chrome://maho-space-config frontend
├── maho_space_create/        # chrome://maho-space-create (theme picker) frontend
├── maho_translate_content_script/ # Page translation content script (injected into pages)
└── maho_welcome/             # Welcome/onboarding screen
```

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Shared UI primitives | `maho_common/react/ui/` | shadcn-spec components; add here, use everywhere |
| Theme tokens / CSS vars | `maho_common/react/theme/tokens.css` | Do NOT define colors per-app |
| Theme bootstrap | `maho_common/react/theme/apply_theme` | Call once on app init; writes `data-theme` to `<html>` |
| DOM extraction adapters | `dom_adapters/` | DOM hierarchy and visual target adapters |
| Mail WebUI frontend | `maho_mail/` | React mail client application |
| App entry | `{feature}/app.ts` | Each feature's SPA entry point |
| Mojo bridge types | `{feature}/maho_{feature}.mojom-webui.ts` | Auto-generated from mojom |
| Boost content script | `maho_boost_content_script/` | Injected into user pages — no Mojo, must be standalone |
| Translate content script | `maho_translate_content_script/` | Injected into user pages — no Mojo, must be standalone |

## CONVENTIONS
- **Shared UI**: use `maho_common/react/ui/`; do NOT create per-feature UI duplicates
- **Styling**: Tailwind v4 utility classes; base theme vars from `tokens.css`; no inline `style={}` for colors
- **Icons**: `lucide-react` (canonical for new surfaces); legacy vendored SVGs in `{feature}/icons/` are deprecated
- **Theme**: every app calls `@theme/apply_theme` on init; do NOT set `data-theme` manually
- **Mojo bridge**: import generated `.mojom-webui.ts` types; do NOT define Mojo types manually
- **Package Resolution & Guard**: Feature bundles resolve npm dependencies from `website/node_modules/` via `tsconfig.json`. Build scripts attach `nodeModulesGuardPlugin` (`maho_common/react/bundle_guard.mjs`) to block invalid imports.
- **Standalone Content Scripts**: `maho_boost_content_script` and `maho_translate_content_script` execute in isolated page worlds and must remain standalone IIFEs without Mojo or external bundler dependencies.

## ANTI-PATTERNS
- No Tailwind v3 patterns — project is on v4 (no `@apply`, different config format)
- No per-app theme token definitions — all tokens live in `maho_common/react/theme/tokens.css`
- No vendored SVG icons in new features — use `lucide-react`
- No `data-theme` attribute manipulation outside `apply_theme`
- No shared components duplicated per-feature — extend `maho_common/react/ui/` instead
- Boost and Translate content scripts must remain standalone — no Mojo imports
- No importing website-only packages (`@astrojs/*`, `astro`, `starlight`, `vite`) into WebUI bundles
- No persisting cleartext passwords or sensitive credentials across unmounts or state resets

## NOTES
- 1400 TS files total; most are in `maho_ai/` and `maho_settings/`
- GN build: each feature has `BUILD.gn`; Tailwind CLI is a GN build step
- `maho_common/components.json` is the shadcn registry; hand-author primitives per spec
