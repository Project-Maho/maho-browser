# Phase D: Shared Atomic UI Primitives — Mobile/Desktop

**Status:** Draft — pending Momus review
**Author:** Phase D execution planning (hyperplan-scoped)
**Date:** 2026-06-19
**Depends on:** Phase B (chat screen complete in web-ai)

---

## Goal

Reduce duplication between mobile `web-ai/` (Preact) and desktop `maho_ai/react/` (React 18) by extracting **framework-agnostic** atomic primitives to a shared TS package. Maintains separate stores, IPC, feature components, and rendering layers per platform.

## Out of Scope (rejected by hyperplan)

| Rejected Item | Reason |
|---------------|--------|
| Sharing `MahoAiStore` | Mojo `PageCallbackRouter` only exists on desktop; no equivalent on mobile |
| Sharing `PageCallbackRouter` bindings | Mojo IDL → TS codegen is Chromium-internal |
| Sharing feature components (agentic copilot ≠ BYOK chat) | Different products with different UX |
| Sharing IPC protocols | Desktop = Mojo, Mobile = postMessage JSON-RPC |
| Sharing `.tsx` components directly | React 18 ≠ Preact 10 (different JSX types, hooks, refs) |

---

## Critical Discovery: React ≠ Preact

Investigation revealed that desktop uses **React 18** (`react-dom/client`, `React.forwardRef`, `@radix-ui/react-slot`, `class-variance-authority`) while mobile uses **Preact 10** (`preact/hooks`, explicit props, no React compat layer).

**Implication:** Shared `.tsx` components are infeasible without a compat shim. The realistic shared surface is:

1. **Pure logic** — framework-agnostic TS functions (markdown parser, utilities)
2. **Data** — icon SVG path records, token values
3. **CSS** — unified design tokens as CSS custom properties

---

## Architecture

```
maho/shared-ui/                         ← NEW: framework-agnostic package
├── src/
│   ├── markdown/
│   │   ├── parse.ts                    ← Pure parser: string → AST (no JSX)
│   │   └── types.ts                    ← MarkdownNode union type
│   ├── icons/
│   │   ├── paths.ts                    ← Record<IconName, string[]> (SVG path data)
│   │   └── types.ts                    ← IconName union type
│   ├── tokens/
│   │   ├── tokens.css                  ← Canonical CSS custom properties
│   │   ├── values.ts                   ← TS export of token values (for JS access)
│   │   └── types.ts                    ← Token name type union
│   └── index.ts                        ← Barrel export
├── package.json                        ← "@maho/shared-ui", type: module
├── tsconfig.json
└── vitest.config.ts

maho/web-ai/                            ← CONSUMER (Preact)
├── src/ui/
│   ├── icon.tsx                        ← imports paths from @maho/shared-ui
│   ├── markdown-renderer.tsx           ← imports AST parser, renders to Preact JSX
│   └── ...
└── package.json                        ← adds "@maho/shared-ui": "workspace:*"

maho-chromium/browser/resources/maho_ai/ ← CONSUMER (React)
├── react/ui/
│   ├── icon.tsx                        ← imports paths from shared, renders via React
│   ├── markdown.tsx                    ← imports AST parser, renders to React JSX
│   └── ...
└── (build config wires shared-ui at GN/Vite level)
```

### Why This Works

- **No framework in shared-ui** — zero React/Preact dependency
- **Each consumer renders the AST/data differently** using its own JSX
- **CSS tokens are framework-agnostic** — consumed via `@import` or build-time copy
- **Icon paths are just data** — `Record<string, string[]>` maps, rendered by platform-specific `<svg>` wrappers

---

## Concrete File Analysis

### Mobile web-ai/src/ui/ — Extraction Candidates

| File | LOC | Extractable to shared? | What moves |
|------|-----|----------------------|------------|
| `icon.tsx` | 122 | **Partial** — SVG path data (lines 35-102) moves to shared; JSX shell stays | `paths` record, `IconName` type |
| `button.tsx` | 85 | **No** — Preact-specific JSX + clsx + inline utility classes | (stays per-platform) |
| `input.tsx` | 49 | **No** — Preact-specific | (stays per-platform) |
| `list-item.tsx` | 44 | **No** — Preact-specific | (stays per-platform) |
| `status-banner.tsx` | 28 | **No** — Preact-specific | (stays per-platform) |

### Mobile chat-screen.tsx — Markdown Parser

| Section | Lines | Extractable? | Notes |
|---------|-------|-------------|-------|
| `parseMarkdown()` | 956-1018 | **Yes** — pure logic, returns JSX but can return AST instead | 62 lines of block-level parsing |
| `parseInlineMarkdown()` | 1020-1084 | **Yes** — pure regex tokenizer | 64 lines of inline parsing |
| `MarkdownRenderer` | 952-954 | **No** — thin Preact JSX wrapper (stays) | 3 lines |

### Desktop maho_ai/react/ui/ — Comparison

| File | Uses external deps? | Sharable overlap with mobile? |
|------|--------------------|-----------------------------|
| `icon.tsx` | Yes (`../../views/icons.js` — Chromium URL system) | Only icon name vocabulary |
| `markdown.tsx` | Yes (`../../views/cards.js` — `renderTrustedMarkdown`) | Only if desktop adopts the shared parser |
| `button.tsx` | Yes (`@radix-ui/react-slot`, `class-variance-authority`) | No overlap — totally different approach |
| `dropdown-menu.tsx` | Likely radix | No mobile equivalent |
| `empty-state.tsx` | Unknown | No mobile equivalent |
| `icon-button.tsx` | Unknown | No mobile equivalent |
| `pill.tsx` | Unknown | No mobile equivalent |
| `popover.tsx` | Unknown | No mobile equivalent |
| `textarea.tsx` | Unknown | No mobile equivalent |

### Token Systems — Current State

| Platform | Namespace | File | Count |
|----------|-----------|------|-------|
| Mobile | `--color-*` | `web-ai/src/styles.css` | ~22 color tokens + spacing via Tailwind-like classes |
| Desktop | `--maho-ai-*` | `maho_ai/styles/tokens.css` | ~80+ tokens (color, spacing, radius, font, shadow, transition) |

**Token overlap (semantic):**
- Surface/background colors: both have canvas, raised, hover, active
- Text hierarchy: both have primary, secondary, muted/tertiary
- Border: both have default border color
- Accent: both have accent + on-accent
- Semantic: both have success, error, warning, info

---

## Migration Path

### Phase D-A — Create shared-ui package (3 days)

| Task | File | Action | Success Criteria |
|------|------|--------|-----------------|
| A1 | `maho/shared-ui/package.json` | Create package `@maho/shared-ui`, `type: "module"`, zero deps | `bun install` succeeds |
| A2 | `maho/shared-ui/src/icons/paths.ts` | Extract SVG path data from `web-ai/src/ui/icon.tsx` lines 35-102 | Exports `Record<IconName, string \| string[]>` |
| A3 | `maho/shared-ui/src/icons/types.ts` | Extract `IconName` union type | Used by both consumers |
| A4 | `maho/shared-ui/src/markdown/types.ts` | Define `MarkdownNode` AST union (Paragraph, CodeBlock, List, InlineCode, Bold, Italic, Link, Text) | Type-only, no runtime |
| A5 | `maho/shared-ui/src/markdown/parse.ts` | Extract `parseMarkdown` + `parseInlineMarkdown` from `chat-screen.tsx` (lines 956-1084), refactor to return AST instead of JSX | Unit tests pass (≥10 cases) |
| A6 | `maho/shared-ui/src/tokens/tokens.css` | Create unified token file; export mobile's `--color-*` set as canonical | File exists, valid CSS |
| A7 | `maho/shared-ui/src/tokens/values.ts` | TS export of token values for programmatic access | Importable, typed |
| A8 | `maho/shared-ui/vitest.config.ts` + tests | Unit tests for markdown parser | `bun run test` green |

### Phase D-B — Wire mobile web-ai as consumer (3 days)

| Task | File | Action | Success Criteria |
|------|------|--------|-----------------|
| B1 | `maho/web-ai/package.json` | Add `"@maho/shared-ui": "workspace:*"` | `bun install` resolves |
| B2 | `maho/web-ai/src/ui/icon.tsx` | Import paths from `@maho/shared-ui/icons` instead of inline record | Icon renders unchanged |
| B3 | `maho/web-ai/src/screens/chat/chat-screen.tsx` | Replace inline `parseMarkdown`/`parseInlineMarkdown` with shared parser + local Preact renderer | Markdown renders unchanged |
| B4 | `maho/web-ai/src/styles.css` | `@import '@maho/shared-ui/tokens.css'` or inline the values (TBD by D-Q1) | Tokens resolve |
| B5 | Verify | `bun run build && bun run test` in web-ai | Bundle size delta ≤ +500 bytes gzipped |

### Phase D-C — Wire desktop maho_ai as consumer (5-7 days)

| Task | File | Action | Success Criteria |
|------|------|--------|-----------------|
| C1 | Chromium build config | Add `shared-ui` as a resolved module in desktop's TS/rollup/esbuild pipeline | `import '@maho/shared-ui'` resolves |
| C2 | `maho_ai/react/ui/icon.tsx` | Import icon name vocabulary from shared (paths still rendered via Chromium URL system) | Compile green |
| C3 | `maho_ai/react/ui/markdown.tsx` | **Optional**: Replace `renderTrustedMarkdown` with shared parser + React renderer | Only if `renderTrustedMarkdown` is a bottleneck or being removed |
| C4 | Desktop token alignment | Map `--maho-ai-*` aliases to shared `--color-*` canonical names where semantically equivalent | Both namespaces coexist; no visual regression |
| C5 | Verify | `autoninja -C out/Default chrome` + manual smoke test of `chrome://maho-ai` | No regressions |

### Phase D-D — Token unification (2-3 days)

| Task | Action | Success Criteria |
|------|--------|-----------------|
| D1 | Define canonical token contract: shared-ui exports the `--color-*` set as the base | Both platforms consume same file |
| D2 | Desktop extends with `--maho-ai-*` (superset for its richer UI) | Desktop tokens.css imports shared + adds desktop-only vars |
| D3 | Mobile drops local `:root` block in favor of shared import | `styles.css` thinner |
| D4 | Visual diff test — screenshot both platforms, compare token-driven elements | No unintended changes |

---

## Risks & Mitigations

| Risk | Severity | Mitigation |
|------|----------|-----------|
| **React/Preact type divergence** — shared package accidentally imports a framework | High | `shared-ui/tsconfig.json` has `"jsx": "preserve"` disabled; no JSX in shared-ui; CI check: `grep -r "from 'react\|from 'preact" shared-ui/src/` must return 0 |
| **Chromium build integration** — adding a workspace dep to maho_ai may conflict with GN/rollup pipeline | Medium | Phase D-C1 is spiked first; if blocked, desktop consumes shared-ui via copy-at-build-time (no workspace resolution needed) |
| **Token rename cascade** — changing `--color-*` to new canonical names breaks existing mobile CSS | Medium | Keep `--color-*` as canonical (it already exists); desktop aliases *to* it, not the other way |
| **Markdown parser fidelity** — extracting from JSX to AST might introduce rendering regressions | Low | Snapshot tests on both platforms; AST → JSX renderers tested in isolation |
| **Bundle size regression** — shared-ui adds overhead if tree-shaking fails | Low | Shared-ui is pure data/functions; Vite/rollup tree-shakes perfectly; CI asserts `≤ +500 bytes gzip` |
| **Version pinning / fate coupling** — shared-ui update breaks one platform | Medium | Shared-ui is workspace-local (not published); changes require both consumers green before merge |

---

## Effort Estimate

| Phase | Duration | Prerequisite |
|-------|----------|-------------|
| D-A: Create shared-ui | 3 days | None |
| D-B: Wire mobile | 3 days | D-A |
| D-C: Wire desktop | 5-7 days | D-A + Chromium build spike |
| D-D: Token unification | 2-3 days | D-B + D-C |
| **Total** | **~2.5-3 calendar weeks** (1 engineer) | Phase B complete |

Lower than original 11-12 week estimate because:
- No store sharing attempted
- No feature component unification
- No IPC protocol alignment
- Scope bounded to data/logic atoms only

---

## Success Criteria

| # | Criterion | Measurement |
|---|-----------|-------------|
| 1 | `@maho/shared-ui` exports ≥3 modules (icons, markdown, tokens) | `import` from each resolves |
| 2 | `web-ai` imports shared-ui; bundle size delta ≤ +500 bytes gzip | CI build metric |
| 3 | Desktop `maho_ai/react/` imports at least icon types from shared-ui | Chromium build green |
| 4 | Single `tokens.css` drives both platform base colors | File audit |
| 5 | Markdown parser has ≥95% test coverage | `vitest --coverage` |
| 6 | No framework imports in `shared-ui/src/` | CI grep guard |
| 7 | Visual snapshot parity on both platforms (before/after) | Screenshot diff ≤ 0.1% |

---

## User Decision Gates

| Gate | Question | Options | Impact |
|------|----------|---------|--------|
| **D-Q1** | Token format — single CSS file vs CSS-in-TS export? | (a) CSS file with `@import` — simpler, standard<br>(b) TS `export const tokens = {...}` — enables typed access<br>(c) Both — CSS file generated from TS source of truth | (c) recommended: TS generates CSS at build time |
| **D-Q2** | Markdown parser scope — extract minimal current impl, or upgrade to full CommonMark? | (a) Extract current 126-line parser as-is (covers `**`, `*`, `` ` ``, `[link](url)`, `- list`, ` ``` code ``` `)<br>(b) Replace with `micromark` or `markdown-it` (~15KB gzip) | (a) recommended: current parser is sufficient, keeps bundle tiny |
| **D-Q3** | Chromium build integration — workspace resolution or copy-at-build-time? | (a) Configure rollup/esbuild to resolve `@maho/shared-ui` from workspace<br>(b) `build_maho.py` copies `shared-ui/dist/` into `browser/resources/` at build time | (b) safer: avoids modifying Chromium's TS compilation pipeline |
| **D-Q4** | Icon vocabulary — union of mobile+desktop names, or only overlapping names? | (a) Union — shared-ui exports superset of all icons used anywhere<br>(b) Intersection — only icons used by both platforms | (a) recommended: simpler maintenance, unused paths tree-shake out |

---

## Workspace Structure

```
maho-workspace/
├── maho/
│   ├── shared-ui/          ← NEW (added to maho/ bun workspace)
│   │   └── package.json    ← "@maho/shared-ui"
│   ├── web-ai/
│   │   └── package.json    ← depends on "@maho/shared-ui": "workspace:*"
│   ├── crates/             ← unchanged
│   └── ...
├── maho-chromium/
│   └── browser/resources/maho_ai/
│       └── (consumes shared-ui via copy or workspace resolution)
└── mail/                   ← potential future consumer (tokens only)
```

**Note:** `shared-ui` is a Bun workspace member under `maho/`, not a standalone workspace. It has zero runtime dependencies. It does NOT need adding to any Cargo.toml.

---

## Appendix: Concrete Shared Artifacts

### A. Icon Path Data (`shared-ui/src/icons/paths.ts`)

```typescript
// Extracted from web-ai/src/ui/icon.tsx — framework-agnostic SVG path data
export type IconName =
  | 'key-round' | 'plus' | 'check' | 'circle-check'
  | 'triangle-alert' | 'trash-2' | 'loader' | 'x'
  | 'chevron-left' | 'circle-alert' | 'download'
  | 'wifi-off' | 'settings' | 'bot'
  // ... desktop-only icons added as needed
  ;

// Each entry is an array of SVG path `d` attributes
export const iconPaths: Record<IconName, string[]> = {
  'key-round': [
    'M2 18v3c0 .6.4 1 1 1h4v-3h3v-3h2l1.4-1.4a6.5 6.5 0 1 0-4-4Z',
  ],
  // ...
};
```

### B. Markdown AST (`shared-ui/src/markdown/types.ts`)

```typescript
export type InlineNode =
  | { type: 'text'; value: string }
  | { type: 'bold'; children: InlineNode[] }
  | { type: 'italic'; children: InlineNode[] }
  | { type: 'code'; value: string }
  | { type: 'link'; href: string; children: InlineNode[] };

export type BlockNode =
  | { type: 'paragraph'; children: InlineNode[] }
  | { type: 'code-block'; language?: string; value: string }
  | { type: 'list'; items: InlineNode[][] };

export type MarkdownAST = BlockNode[];
```

### C. Token CSS (`shared-ui/src/tokens/tokens.css`)

```css
/* Canonical Maho design tokens — consumed by both mobile and desktop */
:root {
  --color-surface: #ffffff;
  --color-surface-raised: #f5f5f7;
  --color-surface-hover: #ebebed;
  --color-on-surface: #1c1c1e;
  --color-on-surface-secondary: #6e6e73;
  --color-border: #d1d1d6;
  --color-accent: #0a84ff;
  --color-on-accent: #ffffff;
  --color-success: #30d158;
  --color-error: #ff453a;
  --color-warning: #ffd60a;
  --color-info: #0a84ff;
  /* ... full set from web-ai/src/styles.css */
}

@media (prefers-color-scheme: dark) {
  :root { /* dark overrides */ }
}
```

Desktop extends with its own `--maho-ai-*` superset for the richer sidebar/panel/glass UI.

---

## Research Notes

### Files in maho_ai/react/ that import Chromium-internal modules

| Import | Used by | Blocks sharing? |
|--------|---------|----------------|
| `chrome://resources/js/load_time_data.js` | `store.ts` | Yes — store is Chromium-only |
| `../../views/icons.js` (getIconUrl, IconSymbol) | `ui/icon.tsx` | Yes — icon rendering is Chromium-only |
| `../../views/cards.js` (renderTrustedMarkdown) | `ui/markdown.tsx` | Yes — markdown rendering is Chromium-only |
| `../../store.js` (MahoAiStore) | `store.ts` | Yes — Mojo-coupled |
| `../../types.js` (AppState, RuntimeEventKind, ViewMode) | `app.tsx` | Yes — desktop feature types |
| `../../views/conversation_thread.js` | `app.tsx` | Yes — desktop feature |

**Conclusion:** Desktop `ui/` components are not directly extractable because they import Chromium-internal modules. Sharing happens at the **data layer** (paths, types, tokens), with each platform providing its own rendering shell.

### Bundle Size Impact Analysis

| Shared artifact | Estimated size (raw) | Tree-shakeable? |
|----------------|---------------------|----------------|
| Icon paths (~14 icons) | ~2.5 KB | Yes (per-icon) |
| Markdown parser | ~3 KB | Yes (whole module) |
| Token CSS | ~2 KB | N/A (CSS, all or nothing) |
| Token TS values | ~1.5 KB | Yes |
| **Total shared-ui** | **~9 KB raw / ~2.5 KB gzip** | — |

Mobile currently includes all of this inline (~12 KB in `icon.tsx` + `chat-screen.tsx` + `styles.css`), so net bundle impact is **negative** (slight reduction from removing duplicated token declarations).
