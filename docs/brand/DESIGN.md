---
version: alpha
name: Maho Marketing
description: Brand-level visual tokens for maho marketing surfaces — website, social cards, GTM content. NOT the product UI design system (desktop/mobile shells have their own platform tokens).
colors:
  primary: "#fafafa"
  secondary: "#a1a1aa"
  tertiary: "#8b7ff5"
  neutral: "#0a0a0a"
  surface: "#fafaf9"
  ink-dark: "#18181b"
  accent-mail: "#5fb3c7"
  border-dark: "#27272a"
typography:
  display:
    fontFamily: Instrument Serif
    fontSize: 6rem
    fontWeight: 400
    lineHeight: 0.96
    letterSpacing: "-0.02em"
  h1:
    fontFamily: Instrument Serif
    fontSize: 3.75rem
    fontWeight: 400
    lineHeight: 0.95
    letterSpacing: "-0.03em"
  h2:
    fontFamily: Instrument Serif
    fontSize: 3rem
    fontWeight: 400
    lineHeight: 1.02
    letterSpacing: "-0.015em"
  subtitle:
    fontFamily: Geist
    fontSize: 1.5rem
    fontWeight: 500
    lineHeight: 1.15
    letterSpacing: "-0.015em"
  body-md:
    fontFamily: Geist
    fontSize: 1rem
    fontWeight: 400
    lineHeight: 1.7
  eyebrow:
    fontFamily: Geist Mono
    fontSize: 0.75rem
    fontWeight: 400
    lineHeight: 1
    letterSpacing: "0.18em"
rounded:
  sm: 8px
  md: 11px
  lg: 17px
spacing:
  sm: 16px
  md: 24px
  lg: 48px
  xl: 96px
components:
  button-primary:
    backgroundColor: "{colors.primary}"
    textColor: "{colors.neutral}"
    rounded: "999px"
    padding: 12px
  button-primary-hover:
    backgroundColor: "{colors.secondary}"
    textColor: "{colors.neutral}"
  card-dark:
    backgroundColor: "{colors.neutral}"
    textColor: "{colors.primary}"
    rounded: "{rounded.lg}"
    padding: 32px
  eyebrow-label:
    textColor: "{colors.tertiary}"
    typography: "{typography.eyebrow}"
---

## Overview

Maho Marketing tokens describe how maho **looks in marketing surfaces**: the Astro website, social/card-news images (satori-rendered), and GTM content. It is deliberately scoped: the product UIs (Chromium desktop WebUI, iOS/Android shells) keep their own platform-native tokens and are NOT governed by this file. See `docs/brand/BRAND.md` for what maho *says*; this file is how it *looks*.

Mood: calm, premium, minimal, editorial. Honest whitespace over decoration. A serif display voice against a precise sans body — editorial gravitas, not SaaS-template gloss.

All values are extracted from the live site (`website/src/styles/global.css`, `marketing.css`) and the verified OG pipeline (`website/scripts/generate-og-images.mjs`) — not invented.

## Colors

- **Primary (#fafafa):** Near-white. Primary text/headlines on dark surfaces (the default marketing canvas is dark).
- **Secondary (#a1a1aa):** Muted gray. Body/sub text, de-emphasized labels.
- **Tertiary (#8b7ff5):** The one brand accent — soft violet. Used sparingly: eyebrow labels, a single hairline, the top-left glow. Never as a full background gradient (that reads as tech-slop).
- **Neutral (#0a0a0a):** Near-black. The default marketing background.
- **Surface (#fafaf9):** Warm off-white for light-mode surfaces (docs, some sections).
- **Ink-dark (#18181b):** Primary text on light surfaces.
- **Accent-mail (#5fb3c7):** Teal, reserved for Maho Mail contexts only. Do not mix with the violet in the same composition.
- **Border-dark (#27272a):** Hairlines and card borders on dark.

Palette discipline: one accent (violet) per composition. Neutrals do the heavy lifting. No rainbow, no secondary accents outside the mail context.

## Typography

Three families, each with a fixed job:

- **Instrument Serif** — display and headings. Editorial voice. `display` is used italic on the site hero (`.landing-display`). This serif is the single strongest brand signal; do not substitute a sans headline.
- **Geist** (Variable) — all body, subtitles, UI-ish text. Precise, neutral, strong numerics.
- **Geist Mono** — eyebrow labels and the `MAHO` wordmark only. Always uppercase, `0.18em` letter-spacing. Never body text.

Hierarchy comes from type scale + weight + spacing before any box, icon, or color is added.

## Layout

- Spacing ladder (marketing sections): inline padding steps 1rem → 1.5rem → 3rem → 6rem across breakpoints; section rhythm 3rem → 6rem block.
- Container max-width: 80rem, centered.
- Social cards: 1080×1080 (1:1), 88px padding. Card-news only — no 9:16/short-form here.

## Shapes

- Squircle radii (superellipse where supported, `corner-shape: superellipse(1)`): factor 1.4 over base — sm 8px, md 11px, lg 17px.
- Pills (999px) for primary buttons.
- Prefer squircles over sharp or fully-round corners; it's a quiet brand tell.

## Components

- `button-primary`: inverted (near-white on near-black), pill, the single high-emphasis action. `button-primary-hover` dims to secondary gray.
- `card-dark`: the base dark card (social cards, feature cards) — neutral bg, primary text, lg squircle radius.
- `eyebrow-label`: violet, Geist Mono, uppercase, wide tracking — the small category label above a heading.

## Do's and Don'ts

**Do**
- Lead visual hierarchy with the serif display + generous whitespace.
- Use violet as a single restrained accent (glow, hairline, eyebrow).
- Show the actual product (screenshots/mockups) when the surface is about a feature.
- Keep motion subtle: 150/200/300ms, `cubic-bezier(0.4, 0, 0.2, 1)`.

**Don't**
- ❌ Full-bleed violet/indigo gradient backgrounds (tech-slop tell #1).
- ❌ Text-only "quote card" slop with no product, no hierarchy beyond a big serif line.
- ❌ Icon-topper feature tiles (rounded-square icon centered over every heading).
- ❌ Mix violet and mail-teal in one composition.
- ❌ Substitute a sans headline for the Instrument Serif display.
- ❌ Invent new accent colors. One accent per composition.

## Landing live browser mock

### Canonical States & Camera Targets
- **hero-rest:** Canonical SSR/first-paint state. Spaces workspace selected, full browser visible at neutral scale/origin (`scale(1.0)`, `transform-origin: 50% 50%`), all scene actions settled, no overlay/panel open.
- **Command Bar product scene:** `/demo/ref-command-bar.png` is the source of truth. Render the actual settings-plus-command-palette capture as one image that owns the full mock window; center-crop only enough to keep the floating palette dominant. Do not recreate settings navigation, search rows, or palette controls with DOM.
- **Split View product scene:** `/demo/ref-split-view.jpeg` is the settled source of truth and must remain fully visible with `object-fit: contain`. During action progress only, `/demo/ref-split-drag.png` may overlay the captured sidebar/neighbor region with a simple opacity/translation handoff. Do not substitute synthetic Wikipedia, Google, or YouTube panes.
- **Agent product scene:** `/demo/ref-agent-panel.png` is the source of truth for the right-docked panel. Compose that image over a subdued real page capture; do not recreate toolbar, quick actions, attachment, composer, or send/stop controls with DOM.
- Camera targets table:
  - **Hero: focus** `(63%, 46%)`, max scale `1.06`
  - **Spaces: focus** `(18%, 58%)`, max scale `1.16`
  - **Split View: focus** `(59%, 50%)`, max scale `1.06`
  - **Command Bar: focus** `(59%, 51%)`, max scale `1.05`
  - **Agent: focus** `(82%, 54%)`, max scale `1.06`

### Phase Maps & Windows
- Hero intro: `establish 0.00-0.14`, `zoom-in 0.14-0.32`, `hold 0.32-0.68`, `zoom-out 0.68-0.84`, `rest 0.84-1.00`.
- Showcase scenes: `establish 0.00-0.12`, `zoom-in 0.12-0.28`, `action 0.28-0.72`, `hold 0.72-0.84`, `zoom-out 0.84-1.00`.
- Handoff timing: **600ms** composed handoff (outgoing settle `0-180ms`, incoming establish `180-600ms`).
- In-scene content transitions: 320-380ms `cubic-bezier(0.22, 1, 0.36, 1)`.

### Playback Modes & Transition Table
- Modes: `autoplay`, `manual`, `paused`, `frozen`.
- Substates: `idle`, `waiting-ready`, `handoff`, `zoom-in`, `action`, `hold`, `zoom-out`, `suspended`.
- Pause/Resume preserves WAAPI `currentTime` and pending timer remainders.

### Lease Arbitration & Thresholds
- Managed by `LandingMotionCoordinator`. Exactly one active surface owns running motion.
- IntersectionObserver thresholds: `[0, 0.1, 0.25, 0.35, 0.4, 0.5, 0.75, 1]`.
- Hero eligibility ratio `>=0.35`, Showcase eligibility ratio `>=0.40`. Owner loss ratio `<0.25`. Manual owner suspension ratio `<0.10`.
- Manual interaction outranks autoplay. When both are eligible, retain current owner until challenger ratio exceeds owner by `>=0.10`.

### Motion Density & Performance Budgets
- At most 1 camera transform, 1 scene-action transform group, and 1 opacity handoff active per surface.
- Smoothness profile (Chromium 1440x1000 DPR1): p95 active-frame cadence `<=18ms`, no interval or LoAF `>50ms`. 4x CPU diagnostic p95 `<=34ms`.
- Respects `saveData` (`navigator.connection?.saveData === true`) and `prefers-reduced-motion: reduce`: static manual state instantly, no autoplay/3D handoff/prewarm.

### Component Architecture & Accessibility
- `MotionRuntime`: WAAPI/compositor layer consuming imperative `SceneMotionSpec`. Zero React commits/mutations during continuous camera/handoff ticks.
- Accessibility: Each browser illustration is exposed as exactly one labelled image (`role="img"` with `aria-label`). Inner visual descendants use `aria-hidden="true"`. All controls are external.
- Reference authority: **Zen** (calm framing, 600ms handoff) and **Aside** (live DOM browser frame, phase state) are `visual-reference-only`.
- Agent marketing limits: User-started, page-aware, controllable current-page analysis. No autonomous action.
- Accepted debt: The three macro-fidelity product scenes intentionally use prepared raster captures. Exact drag-to-split easing/opening behavior is not inferable from static screenshots, so the marketing scene uses only a restrained opacity/translation handoff rather than guessing native motion.

---

*Extracted 2026-07-19 from live website styles + OG pipeline. Scope: marketing/content only. Pairs with `docs/brand/BRAND.md` (strategy/voice). Product UI tokens are out of scope by design. Validate with `npx -y @google/design.md lint docs/brand/DESIGN.md`.*
