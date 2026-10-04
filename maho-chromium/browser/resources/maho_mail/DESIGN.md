# Maho Mail WebUI — Design Contract

Scope: `chrome://maho-mail` (`react/`). Codifies the existing zinc/blue system in
`react/theme/app.css` plus the ergonomic interaction rules added 2026-09-29.

## 1. Color
- Neutrals: zinc scale (`--background`, `--card`, `--muted`, `--border`), dark default, light via `data-theme`.
- One accent ink: `--primary` (blue-500 dark / blue-600 light). State is expressed as alphas of that ink
  (`primary/10` active row, `primary/12` selected nav), never as a second accent.
- Neutral state washes: `--surface-hover` and `--surface-selected` (translucent foreground tint).
- Focus: `--ring` only. Never `ring-accent` (accent equals card in dark and is invisible).

## 2. Typography
- Base 14px (`--maho-base-font-size`), sender 14px semibold when unread / medium when read, snippet 12px.
- Numbers (dates, counts): `tabular-nums`.

## 3. Spacing & hit targets
- Pointer targets >= 28px on desktop (row actions, disclosure chevrons, nav action buttons), >= 44px on touch.
- Resize handles: 8px hit area, 1px hairline visual that appears on hover or drag.

## 4. Primitives & states
- List row: default / hover (`surface-hover`) / active (`primary/10`) / multi-selected (`primary/10`) / unread (6px accent dot + weight).
  No coloured left-border stripes on rows or nav items.
- Nav item: default / hover (`surface-hover`) / current (`primary/12` wash + icon tile `primary/20`, no ring outline).
- Hover-revealed controls use `opacity` and also appear on `focus-visible` / `focus-within`. Never `display:none` toggles,
  because those drop keyboard users.

## 5. Motion
- Tokens: `--mail-ease-out: cubic-bezier(0.22, 1, 0.36, 1)`, `--mail-dur-fast: 120ms`, `--mail-dur-base: 180ms`.
- Only `transform`, `opacity`, `background-color`, `color` animate. Press feedback: `scale(0.97)` on `:active` for buttons.
- `prefers-reduced-motion: reduce` collapses all transitions and animations to ~0ms.

### Reader (EmailContent / EmailBodyView / ConversationThread)
- Subject 20px semibold, balanced wrap, not truncated. Header 24px top padding.
- Plain-text body 15px / 28px line-height, max 72ch measure.
- Thread message headers >= 56px tall, focus ring inset. Attachment actions 36px tall.
- Quick reply: 32px buttons, primary + ghost pair; label chips 28px.

### Compose (ComposeForm / RichTextEditor / ContactAutocomplete / ComposeModal)
- Field rows >= 40px; divider under the focused row turns `primary/50` (focus-within).
- Subject 15px medium, body 15px / 28px.
- Icon buttons 32px in header/toolbar, 40px in footer. Formatting buttons active = `primary/10` + primary ink.
- Recipient chips 28px with a 20px labelled remove button; suggestions are rows >= 40px in a rounded popover.
- Discard hovers destructive. CC/BCC reveal uses `animate-slide-down`.
- Modal: backdrop fades in, panel uses `dialog-enter`, 16px radius.

### All other surfaces (calendar, search, pickers, account setup, onboarding, overlays)
- Icons are Lucide SVGs, never emoji (calendar badges, context menus, conflict warnings, recipient groups).
- Neutral hover everywhere is `bg-surface-hover`; `hover:bg-card`, `hover:bg-muted` and hover-to-solid-primary on ghost/outline are retired.
- Primary buttons use `text-primary-foreground` and `hover:bg-primary/90`; no `text-white`, no amber primary.
- Icon buttons: 32px default, 28px inside dense rows, 44px on mobile day view.
- Menu items (context menu, command, select, pickers) >= 32px tall with rounded-md neutral wash.
- Overlay backdrop `black/55` + blur, dialogs 16px radius with fade/zoom-in.
- No `transition-all`: transitions name their properties.

## 6. Responsive
- >1100px three panes, <=1100px icon rail, <=850px drawer navigation, <768px single pane (unchanged).

## 7. Accessibility constraints
- Visible `focus-visible` ring on every interactive element. Hover-only affordances must be reachable by keyboard.

## 8. Accepted debt
- Mail keeps its own token block in `app.css` instead of `maho_common` `tokens.css` (pre-existing). Migrating it is separate work.
- Sidebar and EmailList still hand-roll button classes instead of the `Button` primitive.
