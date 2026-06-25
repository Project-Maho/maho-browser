# Maho AI Panel: Real shadcn Migration Plan (file-by-file)

**Surface:** `maho-chromium/browser/resources/maho_ai/`
**Companion:** [`maho-ai-shadcn-style-file-by-file-plan.md`](./maho-ai-shadcn-style-file-by-file-plan.md) (the "shadcn-style" baseline that is now mostly on disk).
**Backend:** unchanged. OpenCode + Mojo bridge stay frozen. This is a WebUI/design-system migration only.

## 0. What "shadcn-compatible" means here

Stock shadcn CLI assumes Tailwind, a `components.json` registry, the `@/components/ui` alias, and Tailwind utility classes embedded in `cva` variants. **None of that survives contact with this Chromium WebUI**:

- No Tailwind. CSS is hand-authored against `--maho-ai-*` tokens in `styles/*.css`.
- No `components.json` registry. Files are GN-enumerated in `BUILD.gn` and bundled by `bundle_react.mjs` (esbuild) through `website/node_modules`.
- No alias root. The TS config defines `@ui/*` etc., but every file currently imports via relative paths.
- Icons go through a SF Symbol URL bridge (`chrome://maho-ai/sf-symbol/`), not lucide.

So "real shadcn" here is **the shadcn pattern, applied without Tailwind**:

1. `cn()` is `clsx`. We deliberately skip `tailwind-merge` because there is no Tailwind in this surface and it would only carry weight without value. If a future surface ever mixes utility-flavored classes, swapping in `twMerge` is a one-line change.
2. Variant authoring is `class-variance-authority` (`cva`), not hand-rolled `Record<Variant, string>` maps. Variant *outputs* stay as Maho BEM class names (`compact-icon-button--ghost`, etc.).
3. The base `Button` primitive exposes `asChild` via `@radix-ui/react-slot`, so triggers like `<DropdownMenuTrigger asChild><Button …/></DropdownMenuTrigger>` compose like real shadcn primitives. `IconButton` does not need its own `asChild`: Radix's `asChild` clones whatever child it is given, and `IconButton` already forwards refs and spreads props down to `Button`, so `<DropdownMenuTrigger asChild><IconButton …/></DropdownMenuTrigger>` works today and after this migration.
4. Headless behavior comes from Radix where it is already idiomatic (`@radix-ui/react-dropdown-menu`, the existing `@radix-ui/react-popover`, `@radix-ui/react-tooltip`, `@radix-ui/react-dialog`).
5. We optionally add a `components.json` so the shadcn CLI **could** be pointed here, but we never run `npx shadcn add` to inject Tailwind components. Files stay project-authored.

This is what users mean by "real shadcn" once you strip Tailwind: cva-authored variants, slot-based `asChild`, Radix headless behavior, a single `cn()` helper, and one file per primitive. Stock CLI output is **not** the target because it would import Tailwind and fight the existing token system.

## 1. Current audited state (what is already true)

| Area | State |
|---|---|
| `react/lib/cn.ts` | 3-line `filter(Boolean).join(' ')` helper. Not clsx, not twMerge. |
| `react/ui/button.tsx` | `forwardRef` + hand-rolled `Record<ButtonVariant, string>` map. No `cva`, no `asChild`, no `Slot`. |
| `react/ui/icon-button.tsx` | Composes `<Button>` + `<Icon>`; forces `ariaLabel`. Already forwards ref and spreads props through to `Button`, so it works today as a Radix trigger child via `asChild`. |
| `react/ui/textarea.tsx` | IME-aware; uses `editable-snapshot.ts`. Not built on a primitive layer. |
| `react/ui/popover.tsx` | Wraps `@radix-ui/react-popover`. Portal mounted to `#app`. Forces `align="end"` and a hardcoded `compact-overflow-menu` class. |
| `react/ui/pill.tsx` | Tone string interpolated into class name. No `cva`. |
| `react/ui/empty-state.tsx` | One-shot wrapper around `.empty-panel`. No variants. |
| `react/ui/markdown.tsx` | Wraps `views/cards.ts#renderTrustedMarkdown`. Pure. |
| `react/ui/icon.tsx` | Wraps `views/icons.ts#getSFSymbolUrl`. Pure. |
| `react/features/compact/*` | Already extracted: `composer`, `compact-header`, `compact-shell`, `compact-stage`, `conversation-thread`, `conversation-message`, `conversation-system-item`, `pending-thought`, `overflow-menu`. |
| `react/features/developer/*` | Already extracted: `developer-workspace`, `left-rail`, `timeline`, `inspector`, `event-card`, `mode-toggle`. |
| `react/app.tsx` | Bootstrap shell only (~168 lines). |
| `views/cards.ts` | Trimmed to `renderTrustedMarkdown` + `renderMarkdownInto`. |
| `views/conversation_thread.ts` | Logic-only: `ConversationItem`, `getToolProgressLabel`, `collectConversationItems`. |
| `views/icons.ts` | `getSFSymbolUrl` + `IconSymbol` type. Single source of truth. |
| `styles/*.css` | Split into `tokens / base / compact-panel / thread / composer / developer-console / overflow-menu`. `maho_ai.css` is the import chain. |
| `BUILD.gn` | Inputs enumerate every TSX/CSS file. Will need updates for any new file. |
| `tsconfig.json` | Strict, ES2022, `paths` already include `@ui/*`, `@features/*`, `@lib/*`, `@hooks/*`, plus a `paths` entry for `@radix-ui/react-popover` only. |
| `website/package.json` | Already declares `class-variance-authority`, `tailwind-merge`, `@radix-ui/react-{popover,tooltip,dialog}`, `lucide-react`. **Not** declared: `@radix-ui/react-slot`, `@radix-ui/react-dropdown-menu`, `clsx`. (`tailwind-merge` is declared but intentionally unused by this plan.) |
| Backend | OpenCode adapter, Mojo IDL, `store.ts`, `types.ts` are frozen for this migration. |

So the question is no longer "do we extract components?" but **"do we adopt the real shadcn primitive contract (`cva` + `Slot` + Radix DropdownMenu + a real `cn()`)?"** That is what this plan does.

## 2. Goal (precise)

After this migration:

1. `react/lib/cn.ts` is a thin `clsx` wrapper. All UI files that currently use the local `cn` keep working without changes. `tailwind-merge` is **not** introduced.
2. Every primitive in `react/ui/` that has more than one visual variant is authored with `cva`. Variant *output strings* are the existing Maho BEM class names. No CSS files change as a side effect.
3. `react/ui/button.tsx` accepts `asChild` via `@radix-ui/react-slot`. Trigger composition (`<PopoverTrigger asChild>`, `<DropdownMenuTrigger asChild>`) becomes idiomatic. `IconButton` does not change shape: it already works as a Radix trigger child because it forwards refs and spreads props down to `Button`.
4. `OverflowMenu` is rebuilt on `@radix-ui/react-dropdown-menu` (semantic menu role, arrow keys, type-ahead, focus return). Today it uses Popover with bare `<Button>`s.
5. A `components.json` exists at `maho-chromium/browser/resources/maho_ai/components.json` describing the (non-Tailwind) registry, so the structure is legible to anyone familiar with shadcn even though the CLI is not used.
6. Optional, deferred: `react/ui/{tooltip,dialog,switch,tabs}.tsx` introduced as Radix wrappers when the corresponding feature lands. Not in this migration's scope.

What we are **not** doing:

- Not adopting Tailwind. Not adding `@tailwind` directives. Not generating utility classes.
- Not running `npx shadcn add`. Not pulling in shadcn's pre-baked Button/Dialog source.
- Not changing the SF Symbol icon system. `lucide-react` stays unused for now.
- Not touching Mojo IDL, `store.ts`, `types.ts`, `app.ts`, `bundle_react.mjs`, the OpenCode adapter, `maho/`, `mail/`, `website/`, or `chromium_src/`.
- Not deleting any file currently imported by the React tree.

## 3. shadcn-compatible mapping (this repo)

| shadcn convention | Real implementation here |
|---|---|
| `components/ui/button.tsx` w/ `cva` | `react/ui/button.tsx` w/ `cva` returning Maho BEM classes |
| Tailwind utilities inside variants | Maho BEM class strings inside variants (`compact-icon-button--ghost`, etc.) |
| `cn()` = `clsx` | `react/lib/cn.ts` exports a single `cn` doing exactly that |
| `asChild` via `@radix-ui/react-slot` | `react/ui/button.tsx` accepts `asChild`. `IconButton` rides on it transparently because it spreads props through to `Button`. |
| `DropdownMenu`, `Dialog`, `Popover`, `Tooltip` | Radix wrappers in `react/ui/{dropdown-menu,popover,tooltip,dialog}.tsx` (popover already exists) |
| `components.json` | `maho-chromium/browser/resources/maho_ai/components.json` (informational, not consumed by a build step) |
| `lib/utils.ts` | `react/lib/{cn,format-timestamp,editable-snapshot}.ts` |
| `npx shadcn add` | Not used. Files stay project-authored. |

## 4. File-by-file actions

Legend: **Edit** = scoped change. **Replace** = same file, full rewrite. **New** = create. **Retain** = no change.

### 4.1 Tooling and registry

#### `maho-chromium/browser/resources/maho_ai/components.json` — **New (informational)**

- Purpose: declare the registry so the structure is legible to anyone familiar with shadcn. Not consumed at build time.
- Contents:
  ```json
  {
    "$schema": "https://ui.shadcn.com/schema.json",
    "style": "default",
    "rsc": false,
    "tsx": true,
    "tailwind": null,
    "aliases": {
      "components": "@features",
      "ui": "@ui",
      "lib": "@lib",
      "hooks": "@hooks",
      "utils": "@lib/cn"
    }
  }
  ```
- Acceptance: file exists. Not added to `BUILD.gn` (not a runtime input).

#### `maho-chromium/browser/resources/maho_ai/react/tsconfig.json` — **Edit**

- Add `paths` entries so esbuild and the type checker resolve through `website/node_modules`:
  - `class-variance-authority` -> `../../../../../website/node_modules/class-variance-authority/dist/index.d.ts`
  - `clsx` -> `../../../../../website/node_modules/clsx/dist/clsx.d.ts`
  - `@radix-ui/react-slot` -> `../../../../../website/node_modules/@radix-ui/react-slot/dist/index.d.ts`
  - `@radix-ui/react-dropdown-menu` -> `../../../../../website/node_modules/@radix-ui/react-dropdown-menu/dist/index.d.ts`
- Acceptance: `bun run typecheck` (existing TS gate) succeeds without resolution errors.

#### `website/package.json` — **Edit (single line per dep)**

- Add `clsx` (the new `cn()` depends on it directly).
- Add `@radix-ui/react-slot` (required by `Slot` for `asChild` on `Button`).
- Add `@radix-ui/react-dropdown-menu` (required by the rebuilt overflow menu).
- `tailwind-merge` is **not** added to the runtime path. The dep stays in `package.json` (already declared) but is not imported by `react/`.
- Risk: bundle size delta. All three are tree-shakeable; expect <30 KB combined min+gzip.
- Acceptance: `bun install` clean; `bundle_guard.mjs` does not flag the new packages; metafile inspection after wave 1.

#### `maho-chromium/browser/resources/maho_ai/BUILD.gn` — **Edit**

- Append any new files created by this plan to the `bundle_react` `inputs` list. Confirm completeness against `find react -type f` after each wave.
- Acceptance: `python3 maho-chromium/build/scripts/build_maho.py` exits 0; no GN missing-input warnings.

### 4.2 The `cn()` helper

#### `react/lib/cn.ts` — **Replace**

- Current: `(...values).filter(Boolean).join(' ')`.
- New: thin `clsx` wrapper. Exact contents:
  ```ts
  import {clsx, type ClassValue} from 'clsx';

  export function cn(...inputs: ClassValue[]): string {
    return clsx(inputs);
  }
  ```
- Why `clsx` only: there is no Tailwind here, so `tailwind-merge` would only add weight without value. `clsx` already handles arrays, falsey values, and conditional objects, which is the entire reason call sites use `cn`. If a future surface ever mixes utility-flavored class names, swapping in `twMerge` is a one-line change.
- Caller impact: zero. Every existing call site (`cn('x', cond && 'y', className)`) keeps the same signature.
- Acceptance: `react/ui/*` and `react/features/**/*` unchanged; bundle still builds; `app_bundle.js` size delta inside +5 KB.

### 4.3 Primitives in `react/ui/`

#### `react/ui/button.tsx` — **Replace**

- Replace the hand-rolled variant map with `cva`. `cva` is invoked with the existing Maho BEM strings as the variant outputs. No new selectors. Add `asChild` via `Slot`.
- Skeleton (final shape, to be filled with the existing variant set):
  ```tsx
  import {cva, type VariantProps} from 'class-variance-authority';
  import * as React from 'react';
  import {Slot} from '@radix-ui/react-slot';
  import {cn} from '../lib/cn.js';

  export const buttonVariants = cva('', {
    variants: {
      variant: {
        'console-default': 'console-button',
        'console-primary': 'console-button console-button--primary',
        'compact-icon': 'compact-icon-button',
        'compact-icon-ghost': 'compact-icon-button compact-icon-button--ghost',
        'compact-icon-accent': 'compact-icon-button compact-icon-button--accent',
        'composer-icon': 'compact-composer__icon-button',
        'composer-send': 'compact-composer__send',
        'overflow-item': 'compact-overflow-menu__item',
        'approval-approve':
          'compact-thread__approval-button compact-thread__approval-button--approve',
        'approval-deny':
          'compact-thread__approval-button compact-thread__approval-button--deny',
        'thread-action': 'compact-thread__action-button',
        'inspector-tab': 'inspector-tab',
        'mode-toggle': 'mode-toggle__button',
        'session-list-item': 'session-list__item',
      },
      size: {auto: '', icon: '', small: '', medium: ''},
    },
    defaultVariants: {variant: 'console-default', size: 'auto'},
  });

  export interface ButtonProps
      extends React.ButtonHTMLAttributes<HTMLButtonElement>,
        VariantProps<typeof buttonVariants> {
    asChild?: boolean;
  }

  export const Button = React.forwardRef<HTMLButtonElement, ButtonProps>(
      function Button({asChild, className, variant, size, type = 'button', ...props}, ref) {
        const Comp = asChild ? Slot : 'button';
        return (
          <Comp
            ref={ref}
            className={cn(buttonVariants({variant, size}), className)}
            type={asChild ? undefined : type}
            {...props}
          />
        );
      });

  Button.displayName = 'Button';
  ```
- Acceptance:
  - existing call sites compile (variant union is the same set);
  - `<PopoverTrigger asChild><Button …/></PopoverTrigger>` renders one `<button>` (verify via DOM smoke);
  - keyboard, focus-visible, and aria-pressed semantics unchanged.
- Risk: existing call sites that pass `type` while `asChild` is true would attribute-leak onto `<a>` etc. Mitigated by stripping `type` in the `asChild` branch (shown above).

#### `react/ui/icon-button.tsx` — **Retain**

- No changes. `IconButton` already does `forwardRef` and spreads incoming props onto `Button`, which itself renders a real `<button>` element. Radix's `asChild` clones whatever child you give it and merges props onto the clone, so `<DropdownMenuTrigger asChild><IconButton …/></DropdownMenuTrigger>` works without any modification to `IconButton`. Adding `asChild` to `IconButton` itself would change its single-purpose contract (icon + aria-label + Button) without buying anything.
- Acceptance: import surface unchanged; `<DropdownMenuTrigger asChild><IconButton …/></DropdownMenuTrigger>` (introduced by the rebuilt OverflowMenu in 4.4) renders one `<button>` and forwards focus, click, and keyboard events.

#### `react/ui/pill.tsx` — **Replace**

- Promote the tone interpolation to `cva`. No new tones. Variant outputs are the existing `console-pill` and `console-pill--{tone}` classes.
- Skeleton:
  ```tsx
  import {cva, type VariantProps} from 'class-variance-authority';
  import {cn} from '../lib/cn.js';

  export const pillVariants = cva('console-pill', {
    variants: {
      tone: {
        neutral: 'console-pill--neutral',
        accent: 'console-pill--accent',
        success: 'console-pill--success',
        warning: 'console-pill--warning',
        danger: 'console-pill--danger',
      },
    },
    defaultVariants: {tone: 'neutral'},
  });

  export type PillTone = NonNullable<VariantProps<typeof pillVariants>['tone']>;

  export function Pill(
      {className, text, tone}: {className?: string; text: string} & VariantProps<typeof pillVariants>) {
    return <span className={cn(pillVariants({tone}), className)}>{text}</span>;
  }
  ```
- Acceptance: existing call sites compile; bundle stable.

#### `react/ui/empty-state.tsx` — **Retain**

- No changes. The component currently has exactly one shape used by exactly one set of callers (`Inspector`, `Timeline`). Introducing a `variant` prop now would be speculative work without a real second caller. If a second shape (`'inline'`, etc.) becomes necessary, lift to `cva` at that point as a one-file PR.

#### `react/ui/textarea.tsx` — **Edit (narrow)**

- No structural change. The IME logic is the most fragile code in the panel and stays verbatim.
- Optional: add `asChild` later for embedded composer reuse. Out of scope for this migration. Mark as a TODO comment.

#### `react/ui/popover.tsx` — **Edit (narrow)**

- Today this file forces `className="compact-overflow-menu"` and `align="end"`. Decouple so it is reusable by future popovers:
  - Drop the hardcoded `compact-overflow-menu` class. Move that class onto the *consumer* (overflow menu).
  - Keep portal-into-`#app` behavior (correct for the WebUI host).
  - Allow consumer to pass `align`, `side`, `sideOffset`, `className`.
- Caller impact: `react/features/compact/overflow-menu.tsx` becomes a Popover **consumer** (or, after wave 2, a DropdownMenu consumer; see 4.4).

#### `react/ui/dropdown-menu.tsx` — **New**

- Wrapper around `@radix-ui/react-dropdown-menu`.
- Exports: `DropdownMenu`, `DropdownMenuTrigger`, `DropdownMenuContent`, `DropdownMenuItem`, `DropdownMenuSeparator`, `DropdownMenuLabel`.
- Styling is the existing `compact-overflow-menu`/`compact-overflow-menu__item` classes, applied inside the wrapper. Portal mounted to `#app`.
- Why: gives proper menu semantics (`role="menu"`, `role="menuitem"`, type-ahead, ↑↓ navigation, focus return on Esc) which the current Popover-of-Buttons does not.
- Acceptance: keyboard QA: open with Enter, navigate with ↑/↓, close with Esc, focus returns to trigger.

#### `react/ui/markdown.tsx` and `react/ui/icon.tsx` — **Retain**

- Already minimal and idiomatic. No changes.

### 4.4 Features

#### `react/features/compact/overflow-menu.tsx` — **Replace**

- Today: `Popover` + `<Button variant="overflow-item" role="menuitem">`. This is wrong semantics for a menu (Radix Popover is not a menu).
- New: built on `react/ui/dropdown-menu.tsx`. The trigger uses `<DropdownMenuTrigger asChild><IconButton …/></DropdownMenuTrigger>`. Items become `<DropdownMenuItem>`.
- Acceptance:
  - identical visual output (same CSS classes),
  - keyboard semantics correct,
  - `compact-icon-button` trigger keeps focus-visible style after menu close.

#### `react/features/compact/composer.tsx` — **Edit (light)**

- Update import path of `cn` (still `../../lib/cn.js`).
- After `Button` accepts `asChild`, the send button can stay as-is (no `asChild` needed). No changes required beyond confirming the build.

#### `react/features/compact/compact-header.tsx` — **Retain**
#### `react/features/compact/compact-shell.tsx` — **Retain**
#### `react/features/compact/compact-stage.tsx` — **Retain**
#### `react/features/compact/conversation-thread.tsx` — **Retain**
#### `react/features/compact/conversation-message.tsx` — **Retain**
#### `react/features/compact/conversation-system-item.tsx` — **Retain**
#### `react/features/compact/pending-thought.tsx` — **Retain**

These are already pure compositions of `react/ui/*` primitives. They benefit transparently from the primitive replacement; their source does not change.

#### `react/features/developer/developer-workspace.tsx` — **Retain**
#### `react/features/developer/left-rail.tsx` — **Retain**
#### `react/features/developer/timeline.tsx` — **Retain**
#### `react/features/developer/inspector.tsx` — **Retain**
#### `react/features/developer/event-card.tsx` — **Retain**
#### `react/features/developer/mode-toggle.tsx` — **Retain**

Same rationale. No source changes; they ride on primitive upgrades. `mode-toggle.tsx` and `inspector.tsx` use `cn(active && 'is-active')`, which keeps working under the new `cn`.

### 4.5 Logic helpers (no changes)

#### `views/cards.ts` — **Retain**
#### `views/conversation_thread.ts` — **Retain**
#### `views/icons.ts` — **Retain**
#### `react/lib/format-timestamp.ts` — **Retain**
#### `react/lib/editable-snapshot.ts` — **Retain**
#### `react/hooks/use-app-state.ts` — **Retain**
#### `react/hooks/use-auto-scroll.ts` — **Retain**
#### `react/store.ts` — **Retain**
#### `react/app.tsx` — **Retain**
#### `store.ts`, `types.ts`, `app.ts`, `maho_ai.html` — **Retain**

### 4.6 Styles

No CSS file changes in this migration. The `cva` outputs are the existing Maho BEM class strings.

## 5. Sequencing (waves)

Each wave ends with the same gate set in §6. No wave proceeds until its gate passes.

### Wave A: `cn()` swap and dependency landing

1. Land `clsx` + `@radix-ui/react-slot` + `@radix-ui/react-dropdown-menu` in `website/package.json`. Run `bun install`.
2. Add `tsconfig.json` `paths` entries (`clsx`, `class-variance-authority`, `@radix-ui/react-slot`, `@radix-ui/react-dropdown-menu`).
3. Replace `react/lib/cn.ts` with the `clsx`-only implementation.
4. **Gate.** Bundle size delta inspected. Visual diff: zero.

### Wave B: `cva` adoption in primitives

5. Replace `react/ui/button.tsx` with the `cva` + `Slot` version (adds `asChild`).
6. Replace `react/ui/pill.tsx` with the `cva` version.
7. **Gate.** Visual diff: zero. DOM smoke: `<DropdownMenuTrigger asChild><Button …/></DropdownMenuTrigger>` and `<DropdownMenuTrigger asChild><IconButton …/></DropdownMenuTrigger>` both render one `<button>` with correct focus, click, and keyboard behavior.

### Wave C: DropdownMenu primitive and overflow rebuild

8. Add `react/ui/dropdown-menu.tsx`.
9. Edit `react/ui/popover.tsx` to drop the hardcoded class and forced `align`.
10. Replace `react/features/compact/overflow-menu.tsx` to use DropdownMenu.
11. **Gate.** Keyboard QA on the overflow menu (Enter to open, ↑/↓ to navigate, Esc to close, type-ahead). Pointer QA on click-outside.

### Wave D: registry artifact

12. Add `components.json`.
13. **Gate.** Build green, no functional change.

## 6. Verification gate (every wave)

1. `python3 maho-chromium/build/scripts/build_maho.py` exits 0.
2. `chrome://maho-ai` smoke:
   - panel opens to idle (header + composer only),
   - send a message; assistant response renders as Markdown,
   - tool call: system row appears,
   - approval: approve/reject works,
   - overflow menu opens via mouse and keyboard (after wave C),
   - developer mode toggle works both directions,
   - 340 px and 520 px widths,
   - dark and light themes.
3. `bun run typecheck` clean (proxies to cargo for the Rust workspace; for TS the bundle build is the type gate).
4. esbuild metafile: only intended new packages in `app_bundle.js`. No website-only package leaked.
5. `git diff --stat` matches the file list for the wave.

## 7. Risk register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| `Slot` swallows props or breaks event handlers | Low | Medium | `@radix-ui/react-slot` is widely used. Verify by smoke (focus, click, aria-pressed) on Button after wave B. |
| Radix DropdownMenu portal lands in wrong z-index | Medium | Medium | Mount portal to `#app` (same as Popover). Confirm focus ring positioning. |
| Bundle size growth from new deps | Medium | Low | Tree-shakeable. Inspect metafile after wave A and C. |
| TS path resolution mismatch for new deps | Low | Low | `tsconfig.json` `paths` entries verified at PR time against actual `.d.ts` paths in `website/node_modules`. |
| GN inputs drift after new files | Medium | Medium | `find react -type f` cross-check after each wave that touches `BUILD.gn`. |
| Variant explosion in Button | Low | Low | The variant set is fixed by call sites; `cva` keeps it type-checked. |

## 8. Explicit non-changes

- No Mojo IDL touched.
- No OpenCode adapter touched.
- No upstream Chromium override touched.
- No Tailwind config introduced. No `@tailwind` directives. No utility class generation.
- No change in `maho/`, `mail/`, `website/src/`, or `maho-chromium/components/` source.
- `lucide-react` stays unused. SF Symbol bridge remains the icon system.
- `views/cards.ts` markdown grammar untouched.
- `react/lib/editable-snapshot.ts` IME logic untouched.

## 9. Recommendation

Land waves A through C: real `cn()` (clsx-only), `cva`-authored variants on `Button` and `Pill`, `Slot`-based `asChild` on `Button`, and a Radix `DropdownMenu` for the overflow. They are the minimum that makes this codebase recognizably "shadcn-compatible" without forcing Tailwind. The `components.json` artifact in wave D is cheap and worth landing for legibility.

Defer Tailwind, `tailwind-merge`, lucide, and the rest of the shadcn ecosystem indefinitely. They do not pay rent in a Chromium WebUI that already has a token system, an icon bridge, and a strict GN-enumerated bundle.

## 10. Parking lot

- `react/ui/{tooltip,dialog,switch,tabs}.tsx` Radix wrappers when corresponding features land.
- Promoting stable Maho-AI primitives back into `maho_common/react/primitives/` for cross-surface reuse.
- Lucide icons for non-brand chrome icons if the SF Symbol bridge proves limiting (low priority).
