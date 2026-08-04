# DESIGN.md — Maho Agent (shared mobile Agent screen)

**Surface:** `#agent` route in `maho/web-ai` (Preact SPA).
**Renders on:** iOS `WKWebView`, Android `WebView`. One bundle, one UI, both platforms.
**Design target:** the desktop Chromium `CompactShell` AI panel (`maho-chromium/browser/resources/maho_ai/react/features/compact/`). Mobile is a faithful adaptation of that surface, not a new look.
**Status:** binding contract. A Preact engineer implements against this document with no aesthetic guessing.

> **Reference-only rule.** The desktop React + Tailwind files named throughout this document are the *visual and interaction contract*. They are NOT importable. This bundle is Preact with hand-written CSS custom properties. Never import `react`, `react-dom`, `lucide-react`, `@ui/*`, `cn`, Tailwind, or any desktop file into `src/`. Translate the desktop intent into Preact + `src/styles.css` tokens + inline utility classes and `src/ui/` primitives.

> **Proprietary.** Maho is closed source. No "open source", "MIT", "view source", or public source-repo language anywhere in this surface.

---

## 0. Reference basis (why this is not greenfield)

This is an adaptation task with two concrete references, so the greenfield research lanes do not apply. The binding inputs are:

| Input | Path | Role |
|---|---|---|
| Existing tokens | `src/styles.css` | Canonical `--color-*` token source. Extend, do not replace. |
| Existing Agent screen | `src/screens/agent/agent-screen.tsx` | Current Phase C skeleton. Being upgraded to this contract. |
| Desktop shell | `.../compact/compact-shell.tsx` | Three-zone composition (topbar / stage-or-thread / composer). |
| Desktop topbar | `.../compact/compact-topbar.tsx` | Icon-button row grammar, rounded container, backdrop blur. |
| Desktop empty state | `.../compact/compact-stage.tsx` | Centered card, halo glow, ready copy. |
| Desktop thread | `.../compact/conversation-thread.tsx` | Message list, `ThinkingBubble`, auto-scroll. |
| Desktop message | `.../compact/conversation-message.tsx` | User vs assistant bubble treatment, markdown class map. |
| Desktop pending | `.../compact/pending-thought.tsx` | Streaming placeholder line. |
| Desktop composer | `.../compact/composer.tsx` | Input card, send/stop toggle, model chip. |
| Desktop tokens | `.../maho_common/react/theme/tokens.css` | Semantic naming intent (`card`, `muted`, `primary`, `ring`). Mapped onto web-ai `--color-*`. |
| Desktop app tokens | `.../maho_ai/react/theme/app.css` | `--color-accent-glow`, halo-drift motion. |
| QA screenshots | `maho/reference_screenshots/ai-chat-reference.png`, `preview-ai-panel-reference.png` | **Supporting QA evidence only.** Dark-theme captures used for visual QA sign-off. Where the screenshot and desktop code differ, the desktop code wins; the screenshot's flatter assistant treatment is an accepted lighter-weight rendering, not the contract. |

---

## 1. Identity & atmosphere

Maho Agent is a calm, focused workbench that turns a page or a goal into an autonomous run. The feel is **quiet system utility with one warm accent**: near-neutral surfaces, Apple-system typography, a single blue accent, and depth carried by soft fill-lightness deltas and one faint accent glow rather than heavy shadows.

Adjectives that pass: composed, legible, trustworthy, unhurried, native-feeling.
Adjectives that fail: playful, neon, glassy-maximalist, cluttered, chatbot-cute.

Principles:

1. **Native parity over novelty.** The screen must read as the same product as the desktop compact panel. Same zones, same grammar, same restraint.
2. **Content first.** The conversation is the hero. Chrome (topbar, composer) recedes behind neutral surfaces and thin-stroke icons.
3. **Depth is tonal.** Elevation comes from surface tokens (`surface` < `surface-raised` < `surface-hover`) plus one accent glow behind the empty-state card. Drop shadows are minimal and only on the two floating chrome surfaces (topbar, composer).
4. **One accent.** `--color-accent` (blue) is the only chromatic color in steady state. Everything else is neutral or a semantic status color used sparingly.
5. **No emoji as UI.** No emoji anywhere: not as icons, not as prefixes, not in copy. The current `agent-screen.tsx` renders a `🤖` prefix and this contract removes it in favor of the `bot` SVG from `src/ui/icon.tsx`. SVG icons only.

---

## 2. Color tokens

All colors resolve through `--color-*` custom properties defined in `src/styles.css`. Native shells may inject overrides at load time via `evaluateJavaScript`, and `[data-theme="dark"]` forces dark. Components must reference tokens only. No hard-coded hex, no per-component color literals, no inline `style` for color.

### 2.1 Existing base tokens (canonical, already in `src/styles.css`)

| Token | Light | Dark | Use in Agent |
|---|---|---|---|
| `--color-surface` | `#ffffff` | `#1c1c1e` | Screen background, composer field fill |
| `--color-surface-raised` | `#f5f5f7` | `#2c2c2e` | Assistant bubble, thinking bubble, chrome containers |
| `--color-surface-hover` | `#ebebed` | `#3a3a3c` | Icon-button hover, chip hover |
| `--color-surface-active` | `#e1e1e4` | `#48484a` | Icon-button pressed / active |
| `--color-on-surface` | `#1c1c1e` | `#f5f5f7` | Primary text, message body |
| `--color-on-surface-secondary` | `#6e6e73` | `#aeaeb2` | Labels, timestamps, placeholder, thinking text |
| `--color-on-surface-tertiary` | `#aeaeb2` | `#6e6e73` | Disabled text, faint metadata |
| `--color-border` | `#d1d1d6` | `#38383a` | Hairlines on chrome, bubble edges, dividers |
| `--color-accent` | `#0a84ff` | `#0a84ff` | Send button fill, focus ring, active affordances |
| `--color-on-accent` | `#ffffff` | `#ffffff` | Text/icon on accent fill |
| `--color-success` | `#30d158` | `#30d158` | Complete state marker |
| `--color-success-subtle` | `#d4f5df` | `#0d2e1a` | Complete banner background |
| `--color-error` | `#ff453a` | `#ff453a` | Error banner text/border |
| `--color-error-subtle` | `#ffd6d4` | `#2e0d0b` | Error banner background |
| `--color-warning` | `#ffd60a` | `#ffd60a` | Stop-active affordance tint |
| `--color-warning-subtle` | `#fff5b0` | `#2e2800` | Stop-button background wash |
| `--color-info` | `#0a84ff` | `#0a84ff` | Informational accents (== accent) |
| `--color-info-subtle` | `#d6eeff` | `#001e36` | User bubble background |

### 2.2 New Agent semantic aliases (implementation adds these to `src/styles.css` first)

These map the desktop `CompactShell` semantics onto the existing base tokens so components read intent, not raw surfaces. Add them inside `:root`, the `@media (prefers-color-scheme: dark)` block, and the `[data-theme="dark"]` block. Each alias is defined as a reference to a base token so native theme injection still flows through.

| New alias | Resolves to | Desktop equivalent | Purpose |
|---|---|---|---|
| `--agent-msg-user-bg` | `var(--color-info-subtle)` | `bg-primary/10` | User message bubble fill |
| `--agent-msg-user-fg` | `var(--color-on-surface)` | `text-foreground` | User message text |
| `--agent-msg-assistant-bg` | `var(--color-surface-raised)` | `bg-muted` | Assistant message bubble fill |
| `--agent-msg-assistant-fg` | `var(--color-on-surface)` | `text-foreground` | Assistant message text |
| `--agent-thinking-bg` | `var(--color-surface-raised)` | `bg-secondary/60` | Collapsible thinking bubble |
| `--agent-chrome-bg` | `var(--color-surface-raised)` | `bg-card/90` | Topbar + composer container fill |
| `--agent-chrome-border` | `var(--color-border)` | `border-border/80` | Topbar + composer hairline |
| `--agent-stage-glow` | light `rgba(10,132,255,0.12)` / dark `rgba(212,216,226,0.18)` | `--color-accent-glow` | Radial glow behind empty-state card |
| `--agent-focus-ring` | `var(--color-accent)` | `ring-ring` | Keyboard focus ring on all interactive elements |

Dark-mode value for `--agent-stage-glow` is `rgba(212,216,226,0.18)` to match desktop `app.css` exactly; light-mode uses the accent wash above.

> The dark palette in `styles.css` is the one the QA screenshots exercise. Light mode ships and must be verified, but the screenshots are dark-only, so light-mode visual QA is code/contract-driven.

---

## 3. Typography

Font stack (already set on `body` in `styles.css`, do not change):
`-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif`.

`-webkit-font-smoothing: antialiased` stays on `body`.

| Role | Size | Line height | Weight | Letter-spacing | Case | Token/notes |
|---|---|---|---|---|---|---|
| Stage title (empty state) | 24px / 20px on ≤520px | 30px | 600 | -0.01em | none | `--color-on-surface` |
| Stage description | 14px | 24px | 400 | 0 | none | `--color-on-surface-secondary` |
| Message body | 14px | 24px | 400 | 0 | none | user + assistant |
| Message label ("You" / "Maho") | 11px | 16px | 600 | 0.08em | UPPERCASE | `--color-on-surface-secondary` |
| Timestamp | 11px | 16px | 400 | 0 | none | `--color-on-surface-secondary` |
| Thinking eyebrow ("Thinking") | 10px | 14px | 600 | 0.12em | UPPERCASE | `--color-on-surface-secondary` |
| Thinking body | 12px | 20px | 400 | 0 | italic | `--color-on-surface-secondary` |
| Pending line | 14px | 24px | 400 | 0 | none | `--color-on-surface-secondary`, pulsing |
| Composer input + placeholder | 14px | 24px | 400 | 0 | none | placeholder is `--color-on-surface-secondary` |
| Icon-button label (e.g. "New") | 13px | 18px | 500 | 0 | none | ghost button text |
| Error banner text | 13px | 18px | 500 | 0 | none | `--color-error` |

Markdown inside assistant messages mirrors the desktop `markdownBubbleClassName` map: `h2` 18px/600, `h3` 16px/600, `h4` 14px/600 uppercase 0.08em, `ul`/`ol` disc/decimal at 20px left pad, `li+li` 8px gap, inline `code` 0.85em on `--color-surface-raised` with `--color-border`, `pre` block on `--color-surface` rounded 12px 12px padding, `blockquote` 2px left border in `--color-border`, links in `--color-accent` underlined. The existing `MarkdownRenderer` in `agent-screen.tsx` is the starting point; extend its output to these styles.

---

## 4. Spacing, radius, and mobile layout

### 4.1 Spacing scale (4px base) — add to `src/styles.css`

| Token | Value |
|---|---|
| `--space-1` | 4px |
| `--space-2` | 8px |
| `--space-3` | 12px |
| `--space-4` | 16px |
| `--space-5` | 20px |
| `--space-6` | 24px |
| `--space-8` | 32px |

Horizontal screen inset: 16px (`--space-4`) on phones. Turn-to-turn vertical gap in the thread: 16px (`--space-4`), tightening to 12px (`--space-3`) on ≤520px width, matching desktop `gap-4` / `max-[520px]:gap-3`.

### 4.2 Radius tokens — add to `src/styles.css`

| Token | Value | Use |
|---|---|---|
| `--radius-sm` | 8px | code chips, small controls |
| `--radius-md` | 12px | thinking bubble, pre blocks |
| `--radius-lg` | 16px | message bubbles (desktop `rounded-2xl`), with one squared corner per side (see 5.4) |
| `--radius-chrome` | 20px | topbar container (desktop `rounded-[1.25rem]`) |
| `--radius-composer` | 22px | composer container (desktop `rounded-[1.35rem]`) |
| `--radius-pill` | 9999px | icon buttons, chips, send button |

### 4.3 Safe area + three-zone layout

The screen is a full-viewport flex column with three zones matching `CompactShell`:

```
┌─────────────────────────────┐  ← safe-area-inset-top
│  CompactTopbar (sticky)     │
├─────────────────────────────┤
│  CompactStage  (empty)      │  flex: 1, min-height: 0, scrolls
│    — or —                   │
│  ConversationThread (active)│
├─────────────────────────────┤
│  Composer (sticky bottom)   │
└─────────────────────────────┘  ← safe-area-inset-bottom + keyboard
```

Rules:

- Root: `display:flex; flex-direction:column; height:100dvh; min-height:0;` Use `100dvh` (dynamic viewport) so the mobile URL bar / keyboard resize does not clip the composer.
- Apply safe-area padding via `env(safe-area-viewport-inset-*)` fallbacks: `padding-top: max(var(--space-2), env(safe-area-inset-top)); padding-bottom: max(var(--space-2), env(safe-area-inset-bottom));` on the topbar and composer respectively. Left/right insets: `max(var(--space-4), env(safe-area-inset-left/right))`.
- The middle zone owns `flex:1; min-height:0; overflow-y:auto; -webkit-overflow-scrolling:touch; overscroll-behavior:contain;`. `overscroll-behavior:none` is already on `body`.
- Topbar and composer are the only floating chrome surfaces: fill `--agent-chrome-bg`, 1px `--agent-chrome-border`, `--radius-chrome` / `--radius-composer`, and a single soft shadow `0 20px 60px -48px rgba(8,12,24,0.55)` (desktop uses `-48px rgba(8,12,24,0.85)`; soften slightly for mobile). `backdrop-filter: blur(20px)` where supported; the token fill is the fallback.
- When the software keyboard opens, the composer must stay visible above it. Rely on `100dvh` + native `WKWebView`/`WebView` viewport resize. Do not add JS scroll hacks unless QA shows clipping.

---

## 5. Component tree and states

Component composition mirrors `CompactShell`. Build each as a Preact component under `src/screens/agent/` (or `src/ui/` for reused primitives). Every interactive element gets a `data-testid` (Section 9) and an accessible name.

```
AgentScreen                       [data-testid="agent-screen"]
├── CompactTopbar                 [data-testid="agent-topbar"]
│    ├── icon button: New session [data-testid="agent-new-session"]
│    ├── flex spacer
│    ├── icon button: Settings    [data-testid="agent-settings"]  (optional, if bridge exposes)
│    └── icon button: Close/Back  [data-testid="agent-close"]
├── (empty)  CompactStage         [data-testid="agent-stage"]
│    └── ready card + halo glow + title + description + first prompt hint
└── (active) ConversationThread   [data-testid="agent-thread"]
     ├── ThinkingBubble           [data-testid="agent-thinking-bubble"]  (collapsible reasoning)
     ├── ConversationMessage user [data-testid="agent-message-user"]
     ├── ConversationMessage asst [data-testid="agent-message-assistant"]
     ├── PendingThought           [data-testid="agent-pending-thought"]  (streaming placeholder)
     └── ErrorBanner              [data-testid="agent-error-banner"]
Composer                          [data-testid="agent-composer"]
├── textarea                      [data-testid="agent-composer-input"]
└── send/stop button              [data-testid="agent-send"] / toggles to [data-testid="agent-stop"]
```

### 5.1 CompactTopbar

- Layout: horizontal flex, `gap: var(--space-2)`, container padding `var(--space-2) var(--space-3)`, fill `--agent-chrome-bg`, border `--agent-chrome-border`, `--radius-chrome`, chrome shadow + blur (Section 4.3).
- Left: **New session** icon button (Lucide `plus`; desktop uses `Edit`, but `plus` already exists in `src/ui/icon.tsx` and reads as "new" on mobile). Accessible name "Start new session".
- Center: flexible spacer (`flex:1; min-width:0`). No title text in the bar; identity lives in the stage. (Desktop also keeps the bar title-less.)
- Right cluster (`gap: var(--space-1)`): optional **Settings** (`settings` icon) only if the bridge/route exposes settings navigation; **Close/Back** (`x` icon if the screen is dismissible, else `chevron-left` when `onBack` is provided). Accessible names "Open settings" and "Close" / "Back".
- Icon buttons: see 5.7. Desktop view-mode / tidy-tabs / profile-switcher controls are **out of scope on mobile** (accepted debt, Section 8).

States: default; hover (`--color-surface-hover`, pointer devices only); pressed (`--color-surface-active`, plus subtle `active:scale-95`); focus-visible (2px `--agent-focus-ring`); disabled (`--color-on-surface-tertiary`, 40% opacity, no pointer). New-session is disabled while a run is actively streaming (use the stop control to interrupt first, then start new).

### 5.2 CompactStage (empty / idle state)

Shown when there are no messages and no active thinking label. Mirrors `compact-stage.tsx`.

- Centered card, `max-width: 100%` on phones (desktop caps at `max-w-xl`), fill `--agent-chrome-bg`, border `--agent-chrome-border`, `--radius-chrome`, blur.
- Behind the card, a single radial glow: `radial-gradient(circle at center, var(--agent-stage-glow), transparent 72%)`, blurred, drifting via the `halo-drift` keyframe (Section 6). The glow is decorative, `pointer-events:none`, `aria-hidden`.
- Card content, centered: title + description + a first-run affordance.
  - Title: "Ready when you are" (no live session) or "Live session ready" (session exists).
  - Description: "Describe a task and Maho Agent will run it here." (idle) / "Ask anything about this page to start." (session).
- The idle-state task entry lives in the Composer at the bottom (single input model), not a separate textarea. The current `agent-screen.tsx` uses a distinct idle `<textarea>` + "Run Agent" button; this contract unifies input into the persistent Composer for parity with desktop. The stage is display-only.

States: no-session copy vs session copy (driven by whether a handle/goal exists). Reduced-motion: glow is static (no drift).

### 5.3 ConversationThread (active state)

Shown when `log` has entries or a thinking label is present. Mirrors `conversation-thread.tsx`.

- Column, `gap: var(--space-4)` (`--space-3` ≤520px), vertical scroll, top padding `var(--space-3)`, bottom padding `var(--space-6)`.
- Renders in order: thinking bubbles, user/assistant messages, then the pending placeholder if a turn is mid-flight.
- Sticky auto-scroll: see Section 6.4.

### 5.4 ConversationMessage (user / assistant)

Mirrors `conversation-message.tsx`.

- Wrapper: full width; user turns `justify-items:end`, assistant turns `justify-items:start`.
- Label row above bubble: uppercase label ("You" / "Maho") in secondary text; timestamp appears on the assistant turn and on interaction. On mobile the timestamp is always visible (desktop reveals it on hover; touch has no hover, so it stays visible, matching desktop `max-[520px]:opacity-100`).
- Bubble: `max-width: 92%`, `padding: var(--space-3) var(--space-4)`, `--radius-lg`, `break-word`, 14px/24px text.
  - **User:** fill `--agent-msg-user-bg`, text `--agent-msg-user-fg`, top-right corner squared (`border-top-right-radius: var(--radius-md)`) so it "points" to the sender.
  - **Assistant:** fill `--agent-msg-assistant-bg`, text `--agent-msg-assistant-fg`, top-left corner squared. Assistant bubbles render markdown via the extended `MarkdownRenderer` (Section 3).
- User message controls (edit/delete) are **not** in mobile Agent scope initially (accepted debt, Section 8). Do not render the desktop `UserControls` row.

States: user vs assistant; markdown vs plain; entry animation (fade + 4px rise, `motion-safe` only, Section 6).

### 5.5 ThinkingBubble (collapsible reasoning)

Mirrors the `ThinkingBubble` in `conversation-thread.tsx`. Distinct from the transient PendingThought.

- A full-width button (whole bubble is the toggle), fill `--agent-thinking-bg`, border `--agent-chrome-border`, `--radius-md`, padding `var(--space-2) var(--space-3)`.
- Eyebrow "Thinking" (10px uppercase, secondary), then reasoning text (12px italic, secondary).
- When text length > 80 chars and collapsed, show first 80 chars + ellipsis and a `chevron-left`-family chevron rotated to point down; expanded shows full text and rotates the chevron 180°. If ≤80 chars, no chevron and no toggle affordance.
- The web-ai `Icon` set has `chevron-left`; add a `chevron-down` path to `src/ui/icon.tsx` for this control (rotate on expand).

States: collapsed / expanded; toggleable / non-toggleable (short text); focus-visible ring; entry animation (fade + rise, motion-safe).

### 5.6 PendingThought (streaming placeholder)

Mirrors `pending-thought.tsx`. A single line of secondary text (e.g. "Thinking…" or the streaming label), `padding-inline: var(--space-1)`, 14px/24px, gently pulsing via `motion-safe:animate-pulse`. No border, no fill. Reduced-motion: static, no pulse. This is the live "turn in progress" affordance; it is replaced by real content as tokens arrive.

### 5.7 Icon buttons (shared primitive)

Add to `src/ui/` (e.g. `icon-button.tsx`). Circular ghost button.

- Size: **44x44px minimum hit target** (WCAG 2.2 AA, 2.5.8). Visual glyph 18-20px via `Icon`. If the visual footprint is smaller, expand the tappable area with padding so the hit target is ≥44px.
- Shape: `--radius-pill`. Idle: transparent fill, icon in `--color-on-surface-secondary`.
- Hover (pointer only): `--color-surface-hover`, icon to `--color-on-surface`.
- Pressed: `--color-surface-active`, `transform: scale(0.95)` (motion-safe).
- Focus-visible: 2px `--agent-focus-ring` offset ring.
- Disabled: icon `--color-on-surface-tertiary`, 40% opacity, `pointer-events:none`, `aria-disabled`.
- Every icon button has an `aria-label`; the `Icon` inside is `aria-hidden`.

### 5.8 Composer

Mirrors `composer.tsx`, reduced to the mobile-relevant surface. This is the single input for both idle task entry and in-run follow-ups.

- Container: fill `--color-surface` inside the chrome (desktop `bg-card/95`), border `--agent-chrome-border`, `--radius-composer`, chrome shadow + blur, padding `var(--space-2)`. Grid: textarea row, then a control row.
- Textarea (`agent-composer-input`): starts at a single line (`rows=1`, no min-height), auto-grows with content (JS sets height to `scrollHeight`) up to `max-height: 40vh` then scrolls. Because the composer is bottom-anchored, growth pushes upward. No border/own background (transparent on the container), `padding: var(--space-2)`, 14px/24px. Placeholder "Describe a task for the agent…" (idle) / "Ask a follow-up…" (active session). `resize:none`.
- Control row (`gap: var(--space-2)`, items centered): optional leading **add-context** icon button (`plus`) only if a bridge context API exists on mobile (otherwise omit; accepted debt Section 8). A flexible spacer. Trailing **send/stop** button.
- **Send/stop button** (`agent-send` / `agent-stop`): circular, 44px hit target, `--radius-pill`.
  - Send (idle or ready): fill `--color-accent`, icon in `--color-on-accent`. Use an up-arrow / send glyph (add a `send` or `arrow-up` path to `src/ui/icon.tsx`; screenshots show an up-arrow send). Disabled when the input is empty and no run is active: fill `--color-surface-active`, icon `--color-on-surface-tertiary`.
  - Stop (run active): fill `--color-warning-subtle`, border `--color-warning`, a filled square `x`-family "stop" glyph tinted `--color-warning`. This is the desktop `canCancel` state. Accessible name toggles between "Send message" and "Stop current run".
  - Pressed: `active:scale-95` (motion-safe).

States: empty (send disabled); has-text (send enabled); creating/streaming (stop shown, send hidden); complete/cancelled/error (send enabled for a new prompt); read-only (not applicable to mobile Agent initially).

### 5.9 ErrorBanner

Mirrors the desktop `Alert` usage and the current `agent-error-banner`. Full-width, `role="alert"`, fill `--color-error-subtle`, 1px border `--color-error`, `--radius-md`, `padding: var(--space-2) var(--space-3)`, text `--color-error` 13px/500, with a leading `circle-alert` or `triangle-alert` icon. Appears at the top of the thread zone when `phase === 'error'`. A completed run may also surface a subtle success affordance using `--color-success` / `--color-success-subtle`, but success is quiet (no persistent banner required).

---

## 6. Motion & interaction contract

### 6.1 Motion inventory

| Motion | Trigger | Spec | Reduced-motion |
|---|---|---|---|
| Message enter | new message mounts | fade-in + 4px slide-up, ~200ms ease-out, `transform`/`opacity` only | none (instant) |
| Thinking bubble enter | mount | same fade + rise | none |
| Pending pulse | turn in progress | opacity pulse, ~1.5s loop | none (static) |
| Halo drift | empty-state glow | `halo-drift` 24s linear infinite (port from `app.css`) | none (static) |
| Chevron rotate | thinking expand/collapse | 180° rotate, ~150ms | still rotates (state indicator), but instant |
| Button press | tap | `scale(0.95)`, ~100ms | none |

All animation is GPU-composited (`transform`, `opacity`, `filter`). Never animate layout properties (width/height/top/left). Gate every decorative animation behind `@media (prefers-reduced-motion: reduce)` to disable, following the pattern already in `app.css`. The existing `.animate-spin` in `styles.css` stays for loaders.

Motion must serve meaning: entry animations signal new content, the pulse signals an in-flight turn, the chevron signals expand state. No decorative motion on non-interactive, non-changing elements.

### 6.2 Composer keyboard contract

Port the desktop `ComposerTextarea` behavior exactly:

- **Enter** submits the current prompt (when non-empty and not composing).
- **Shift+Enter** inserts a newline (never submits).
- **IME guard:** while a composition is active, Enter must NOT submit. Track `compositionstart` / `compositionend` (and check `event.isComposing`) and ignore Enter submits during composition. This is mandatory for Korean, Japanese, and Chinese input where Enter confirms candidates. This is critical on mobile keyboards.
- Submitting clears the input and starts/continues the run.
- Empty or whitespace-only input never submits.

### 6.3 Run lifecycle & controls

Driven by `AgentPhase` (`idle | creating | streaming | complete | cancelled | error`) and the bridge agent API (`agentCreateSession`, `agentSendMessage`, `agentPollEvent`, `agentCancel`, `agentFreeSession`).

- **Submit (idle):** trim prompt; if empty, no-op. Dispatch `CREATING`, `agentCreateSession(uuid)`, then `agentSendMessage(handle, prompt)`. Failure dispatches `AGENT_ERROR`.
- **Send/Stop toggle:** while `creating` or `streaming`, the composer button is the **Stop** control (`agentCancel`, dispatch `CANCEL`). Otherwise it is **Send**.
- **New session:** topbar new-session button frees the current handle (`agentFreeSession`) if any, then `RESET` to idle. Disabled while actively streaming (stop first).
- **Goal auto-submit:** when the screen mounts from `#agent?goal=<url-encoded text>`, decode the goal, seed the composer, and auto-submit exactly once (same path as manual submit). Guard against re-submit on re-render or hash change echoes. If no goal param, land in idle stage.
- **Free on unmount:** always `agentFreeSession(handle)` on unmount (already implemented via `handleRef`). Keep this.

### 6.4 Sticky auto-scroll

Replace the current unconditional `scrollTop = scrollHeight` with **threshold-based sticky scroll** matching desktop `useAutoScroll`:

- Track whether the user is "pinned" to the bottom: pinned when `scrollHeight - scrollTop - clientHeight <= 48px`.
- On new tokens/messages, auto-scroll to bottom **only if pinned**. If the user has scrolled up to read history, do not yank them down.
- When a new user message is sent, always scroll to bottom (the user just acted).
- Consider an optional "jump to latest" affordance when unpinned and new content arrives (desktop uses a `ChevronDown` control). This is nice-to-have, not required for the first cut.

---

## 7. Accessibility constraints (WCAG 2.2 AA)

- **Contrast:** body text on its bubble fill and all label/secondary text must meet 4.5:1 (normal) / 3:1 (large ≥24px or ≥18.66px bold). The neutral token pairs in Section 2 satisfy this in both themes; verify `--color-on-surface-secondary` on `--color-surface-raised` specifically (thinking + timestamp) during QA.
- **Touch targets (2.5.8):** every interactive control ≥44x44px. Applies to icon buttons, send/stop, and the thinking-bubble toggle.
- **Focus visible (2.4.7 / 2.4.11):** 2px `--agent-focus-ring` on every focusable element, never fully obscured. Logical tab order: topbar left→right, then thread (if focusable controls exist), then composer input, then send.
- **Names & roles:** icon-only buttons carry `aria-label`; decorative icons and the glow are `aria-hidden`. The thread container uses `role="log"` `aria-live="polite"` so streamed tokens are announced without spamming; the error banner uses `role="alert"` (assertive). Do not double-announce: the pending pulse text should be inside the polite live region, not a separate assertive one.
- **Reduced motion (2.3.3):** honor `prefers-reduced-motion: reduce` for every animation in Section 6.
- **Text scaling / reflow (1.4.10 / 1.4.4):** layout must survive up to 200% text zoom and dynamic-type without horizontal scroll; bubbles wrap, the composer grows, nothing clips. Use relative units and `max-width`, never fixed pixel heights on text containers.
- **Orientation (1.3.4):** works in portrait and landscape; do not lock orientation in the web layer.
- **Color is not the only signal:** send vs stop differ by glyph (arrow vs square) and label, not only by warning tint; complete/error differ by icon + text, not color alone.

---

## 8. Personas, constraints, and accepted debt

### 8.1 Personas / usage constraints driving the design

- **One-handed mobile operator.** Primary actions (send/stop) sit at the bottom-right within thumb reach. The new-session control is top-left (reachable) but destructive-adjacent, so it is disabled mid-stream to prevent accidental loss. Bottom-anchored composer keeps the write action in the thumb zone.
- **Assistive-tech user (VoiceOver / TalkBack).** Streaming output flows through a single polite live region so tokens are announced coherently; errors interrupt via `role="alert"`. All controls are reachable and labeled. No emoji that would be read aloud as decorative noise.
- **IME / CJK typist.** Enter-to-send is fully guarded behind composition state so candidate confirmation never fires an accidental send.
- **Low-vision user.** Honors dynamic type / 200% zoom, high-contrast neutral tokens, and reduced-motion.
- **Bright-sunlight / glance use.** High-contrast text, one clear primary action, minimal chrome, no reliance on subtle hover states (touch has none).

### 8.2 Accepted debt (explicit, revisitable)

1. **No tool approval or activity cards on mobile.** The native mobile agent kernel exposes only `token`, `complete`, and `error` events (see `use-agent-session.ts` and `bridge/types.ts` `AgentEvent`); tool calls are auto-approved/denied by static native rules and are NOT surfaced. Therefore the desktop `TrustCeremony`, `ConversationSystemItem`, approval prompts, and tool-activity cards are **intentionally absent**. `agentListTools` exists but is not rendered. Revisit when the native bridges emit tool-activity events.
2. **No context picker / attachments.** The desktop composer's tabs/history/current-page/file picker (`Popover` + sections) is out of scope for the first mobile cut. The leading `plus` control is only added if and when a mobile context bridge lands.
3. **No model switcher UI.** The desktop model dropdown is omitted; mobile uses the active model configured elsewhere.
4. **No view-mode / tidy-tabs / profile-switcher** in the topbar (desktop-only affordances).
5. **No per-message edit/delete controls.** Desktop `UserControls` is not rendered on mobile initially.
6. **Light-theme QA is contract-driven.** The reference screenshots are dark-only, so light-mode sign-off leans on this contract plus code inspection rather than a captured screenshot.

Each item is a scoped omission, not a redesign. Nothing here changes the core three-zone shell or the token system.

---

## 9. Test IDs (required)

Implementation must expose these stable `data-testid` hooks (tests query by role/label first per AGENTS.md, and by test-id for structure):

| Test ID | Element |
|---|---|
| `agent-screen` | Root screen (already present) |
| `agent-topbar` | Topbar container |
| `agent-new-session` | New-session icon button |
| `agent-settings` | Settings icon button (if rendered) |
| `agent-close` | Close / back icon button |
| `agent-stage` | Empty-state stage |
| `agent-thread` | Active conversation list (`role="log"`) |
| `agent-thinking-bubble` | Collapsible reasoning bubble |
| `agent-message-user` | A user message article |
| `agent-message-assistant` | An assistant message article |
| `agent-pending-thought` | Streaming placeholder line |
| `agent-error-banner` | Error alert (`role="alert"`) |
| `agent-composer` | Composer container |
| `agent-composer-input` | Prompt textarea |
| `agent-send` | Send button (send state) |
| `agent-stop` | Send button (stop state) |

Accessible names (queryable by `getByRole('button', { name })`): "Start new session", "Open settings", "Close" / "Back", "Send message", "Stop current run", plus the textarea's "Task description" / "Message" `aria-label`.

---

## 10. Visual QA requirements

Run `/visual-qa` on the built bundle in a real browser at **375px, 390px, and 768px** widths (phone + small tablet), both `prefers-color-scheme` values and the forced `[data-theme="dark"]` path. Compare against `maho/reference_screenshots/ai-chat-reference.png` and `preview-ai-panel-reference.png` for the dark active + empty states. Required states to capture and verify:

1. **Idle / empty stage** — centered ready card, halo glow present (and static under reduced-motion), composer visible and empty, send disabled.
2. **Goal auto-submit** — screen entered via `#agent?goal=...`, prompt seeded, run starts once.
3. **Streaming** — pending pulse visible, tokens accumulating into an assistant bubble, stop control shown, thread pinned to bottom.
4. **Sticky-scroll behavior** — scroll up mid-stream and confirm the view does NOT snap to bottom; send a new message and confirm it does.
5. **Completed turn** — assistant markdown rendered (headings, list, code block, link) per Section 3.
6. **User + assistant bubbles** — correct alignment, corner-squaring, fills, and label/timestamp rows in both themes.
7. **Thinking bubble** — collapsed (>80 chars, ellipsis + chevron) and expanded.
8. **Error** — `agent-error-banner` with `role="alert"`, correct error tokens.
9. **Keyboard/IME** — Enter submits, Shift+Enter newlines, CJK composition Enter does not submit.
10. **Safe area** — topbar clears the notch/status bar, composer clears the home indicator, composer stays above the software keyboard (no clipping).
11. **Touch targets** — every control ≥44px (overlay/inspect).
12. **Reduced motion** — all decorative motion disabled, chevron still indicates state.

Sign-off is the `/visual-qa` dual-oracle gate on fresh evidence, not a single glance. Where a capture diverges from the desktop code intent, the desktop code is the contract and the screenshot is advisory.

---

## 11. Implementation guardrails (recap)

- Preact only. No `react`/`react-dom`. `jsxImportSource: "preact"`, hooks from `preact/hooks`.
- No Tailwind. Hand-written inline utility classes + `src/styles.css` tokens.
- No `lucide-react`. Extend `src/ui/icon.tsx` (`chevron-down`, `send`/`arrow-up` paths needed).
- No external UI library; add primitives to `src/ui/`.
- Explicit prop interfaces (do not extend `JSX.HTMLAttributes`).
- `useReducer` for screen state (the existing reducer stays and grows).
- Bridge calls are async; use the `void fn()` / `useCallback` patterns.
- No emojis anywhere. Remove the `🤖` prefix in `agent-screen.tsx`; use the `bot` SVG.
- Every color from a `--color-*` token or a Section 2.2 alias. No hex literals, no inline color styles.
- Closed source: no OSS/source-repo language in copy or comments.

---

## 12. AI Settings (`#byok`) extension

The `#byok` route is the mobile adaptation of the desktop Chromium AI settings pane. It reuses the existing neutral surface hierarchy, 4px spacing scale, radii, focus ring, shared `Input`, `Button`, `ListItem`, `StatusBanner`, and inline `Icon` primitives.

- Provider selection is a native labeled select with Maho Managed, OpenAI, Anthropic, and OpenAI-compatible options.
- Settings content is capped by `--settings-content-max: 48rem` to match the desktop pane's readable width while remaining full-width on mobile.
- All text inputs use `--font-size-control: 16px` and `--line-height-control: 24px` to avoid iOS focus zoom.
- Credential fields appear only for BYOK/custom providers. Stored secrets are represented by status copy and an empty replacement field, never by returning the secret to the UI.
- Base URL and model persist on blur; credentials persist through explicit Save/Clear actions. Base URL is sent exactly as typed.
- The managed and unset provider states show a quiet managed-relay status row with no credential fields.
