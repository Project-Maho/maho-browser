# Zen Browser Onboarding: Pixel-Level Reference Dossier

**Repo**: `zen-browser/desktop` · **HEAD SHA**: `d5cbe55d2b16a599e59b2a25ae9df37270bb5bc4`

This dossier provides exact values (pixels, hex colors, animation params, copy text, file paths) extracted from Zen's onboarding implementation. Used as the input for Maho's Tier A visual parity spec.

---

## Source File Map

| Role | Zen Path |
|------|---------|
| Onboarding JS (all logic) | `src/zen/welcome/ZenWelcome.mjs` |
| Onboarding CSS | `src/zen/welcome/zen-welcome.css` |
| Localization | `locales/en-US/browser/browser/zen-welcome.ftl` |
| Resource manifest | `src/zen/welcome/jar.inc.mn` |
| Theme tokens | `src/zen/common/styles/zen-theme.css` |
| Branding/typography | `src/zen/common/styles/zen-branding.css` |
| Buttons | `src/zen/common/styles/zen-buttons.css` |
| Animations | `src/zen/common/styles/zen-animations.css` |
| Theme picker XUL | `src/browser/base/content/zen-panels/theme-picker.inc` |
| Gradient engine | `src/zen/spaces/ZenGradientGenerator.mjs` |
| Gradient picker CSS | `src/zen/spaces/zen-gradient-generator.css` |
| Dark scheme | `src/zen/common/styles/schemes/dark.inc.css` |
| Light scheme | `src/zen/common/styles/schemes/light.inc.css` |

---

## Window & Stage

- **Initial size**: `875×560px`, centered on screen (`window.resizeTo(875, 560)`)
- **Stage attribute**: `zen-welcome-stage` on `<html>` for full duration; removed in `finish()`
- **Background**: `var(--zen-branding-bg)` = light `#e2e2e2` / dark `#101010`
- **Font**: `Zen-Junicode` serif (custom WOFF2 `JunicodeVF-Roman.woff2`, weight 400, `font-feature-settings: "swsh" 1`)

---

## SCREEN 0 — Splash

### Copy
- Line 1: `Welcome to`
- Line 2: `a calmer internet`
- CTA: icon-only forward arrow (`chrome://browser/skin/zen-icons/forward.svg`)

### Layout
```
#zen-welcome-start (flex column, centered)
├── #zen-welcome-title (5rem / 500 / line-height 1.1 / Junicode / max-width 50%)
│   ├── <span>Welcome to</span>     (display:block)
│   └── <span>a calmer internet</span>
└── #zen-welcome-start-button       (absolute bottom:10%, padding:1em, min-width:100px)
```

### Title Animation (Motion One spring)
```js
animate("#zen-welcome-title span",
  { opacity: [0,1], y: [20,0], filter: ["blur(2px)","blur(0px)"] },
  { delay: stagger(0.6, {startDelay: 0.2}), type: "spring", stiffness: 300, damping: 20, mass: 1.8 });
```
- Span 1 enters at **0.2s**
- Span 2 enters at **0.8s** (0.6s stagger)
- CTA enters at **0.1s** after spans complete

### Exit Animation (on button click)
```js
animate("#zen-welcome-title span, #zen-welcome-start-button",
  { opacity: [1,0], y: [0,-10], filter: ["blur(0px)","blur(2px)"] },
  { type: "spring", ease: [0.755, 0.05, 0.855, 0.06], bounce: 0.4, delay: stagger(0.4) });
```

---

## SHELL (steps 1-5)

```
┌────────────────────────────────────────────────────────┐
│              #zen-welcome-pages                        │
│  width: 60% × 60% (responsive: 80%/90%/100%)           │
│  border-radius: 1em (16px)  shadow: 0 3px 8px rgba(.24)│
│  background: var(--zen-branding-bg)                    │
│  ┌─────────────────────┬──────────────────────────┐   │
│  │  page-sidebar 40%   │  page-content 60%        │   │
│  │  padding: 3.8rem    │  background: rgba(.1)    │   │
│  │                     │  gap: 1.6rem             │   │
│  │  ┌ sidebar-content  │                          │   │
│  │  │   <h1>           │                          │   │
│  │  │   <p> body 1     │                          │   │
│  │  │   <p> body 2     │                          │   │
│  │  │                  │                          │   │
│  │  │ sidebar-buttons  │                          │   │
│  │  │   [primary]      │                          │   │
│  │  │   [skip]         │                          │   │
│  └──┴──────────────────┴──────────────────────────┘   │
└────────────────────────────────────────────────────────┘
```

### Per-Step Page Transitions

| Animation | Property | From → To | Config |
|---|---|---|---|
| Shell enter | `#zen-welcome-pages` opacity | `0→1` | delay 0.2s, duration 0.1s |
| Sidebar h1+p+p enter | `x` | `"150%"→0` | spring bounce:0.2, stagger:0.05s |
| Buttons enter | `x` | `"150%"→0` | spring bounce:0.2, stagger:0.1s, startDelay:0.4s |
| Content enter | `opacity` | `0→1` | spring bounce:0.2, stagger:0.1s |
| Sidebar exit | `x` | `0→"-150%"` | spring bounce:0, stagger:0.05s, startDelay:0.3s |
| Buttons exit | `x` | `0→"-150%"` | spring bounce:0, stagger:0.1s, startDelay:0.4s |
| Content exit | `opacity` | `1→0` | spring bounce:0, stagger:0.05s, startDelay:0.3s, duration:0.1s |

Buttons get `pointer-events: none` until entry animation completes.

---

## SCREEN 1 — Import + Default Browser

### Copy
- Headline: `A Fresh Start, Same Bookmarks`
- Body 1: `Your bookmarks, history, and passwords are like a trail of breadcrumbs through the internet—don't leave them behind!`
- Body 2: `Easily bring them over from another browser and pick up right where you left off.`
- Primary CTA: `Import now` → after click replaced with `Next`
- Secondary CTA: `Skip`

### Content Area: TWO radio cards rendered SIDE-BY-SIDE
- "Set Zen as your default browser"
- "DON'T set Zen as your default browser" (default checked)

### Card CSS
```css
label {
  padding: 1.5rem 1.1rem;        /* 24px 17.6px */
  border-radius: 0.6rem;          /* ~9.6px */
  width: 50%;
  gap: 0.8rem;
  border: 2px solid var(--zen-colors-border);
  background: light-dark(rgba(255,255,255,0.7), rgba(0,0,0,0.4));
  &:has(:checked) {
    box-shadow: 0 3px 8px rgba(0,0,0,.24);
    border: 2px solid var(--zen-primary-color);
    scale: 1.03;
  }
  &:hover { box-shadow: 0 3px 8px rgba(0,0,0,.24); }
  transition: scale 0.1s, box-shadow 0.1s;
}
```

**Note**: Import action delegates to Gecko's `MigrationUtils.showMigrationWizard(window, {isStartupMigration:true})` — opens native migration UI (Zen does NOT custom-style it).

---

## SCREEN 2 — Default Search Engine

### Copy
- Headline: `Your Default Search Engine`
- Body: `Choose your default search engine. You can always change it later!`
- CTA: `Next`

### Engine Grid
```css
display: grid;
grid-template-columns: 1fr 1fr;
padding: 5rem;                   /* 80px all sides */
> label {
  flex-direction: column;
  font-weight: 600;
  aspect-ratio: 1 / 0.7;
  width: 60%;
  /* if 3+ engines: height: 50% */
}
```

Each engine = 32×32 icon + name label radio card.
Filter: removes engines with name containing "wikipedia" or "ebay".
Click immediately calls `SearchService.setDefault(engine.originalEngine, USER)`.

---

## SCREEN 3 — Essentials (Mock Browser Preview)

### Copy
- Headline: `Your Key Tabs, Always Within Reach`
- Body 1: `Keep your most important tabs easily accessible and always at hand, no matter how many you open.`
- Body 2: `Essential tabs are always visible, no matter what workspace you are in.`
- CTA: `Next`

### Pre-seeded 9 essential apps
| App | URL | Icon path |
|---|---|---|
| Obsidian | obsidian.md | obsidian.svg |
| Discord | discord.com | discord.svg |
| Trello | trello.com | trello.svg |
| Slack | slack.com | slack.svg |
| GitHub | github.com | github.svg |
| Tuta | app.tuta.com | tuta.svg |
| Notion | notion.com | notion.svg |
| Google Calendar | calendar.google.com | calendar.svg |
| Figma | figma.com | figma.svg |

(Icons from `chrome://browser/content/zen-images/favicons/{name}.svg`)

### Mock Browser CSS
```css
#zen-welcome-initial-essentials-browser {
  min-width: 70%;
  min-height: 80%;
  margin-left: auto; margin-top: auto;
  border-top-left-radius: 1.2em;
  box-shadow: 0 3px 8px rgba(0,0,0,.24);
  background: light-dark(rgba(255,255,255,.5), rgba(0,0,0,.2));
  padding-right: 20%;            /* simulates content area crop */
  border: 1px solid var(--zen-colors-border);
  border-bottom-width: 0;
  border-right-width: 0;
}
#sidebar {
  padding: 1.4rem; gap: 1.2rem;
  background: light-dark(rgba(255,255,255,.7), rgba(0,0,0,.4));
}
#win-buttons {                   /* macOS-style buttons */
  gap: 0.5rem;
  > div { width: 15px; height: 15px; border-radius: 50%; }
}
#essentials-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 1rem;
}
.tabbrowser-tab {
  --tab-min-height: 6rem !important;   /* 96px */
  min-width: 5rem !important;          /* 80px */
  --border-radius-medium: 1rem;
  &[visuallyselected] { transform: scale(1.06); }
  &::after {
    width: 1.8rem; height: 1.8rem;
    background: var(--zen-essential-tab-icon);
    border-radius: 4px;
  }
}
.extra-tab {
  width: 100%; height: 4rem;
  border-radius: var(--border-radius-large);
  background: var(--zen-toolbar-element-bg);
  grid-column: span 3;
}
```

### Selection Behavior
- Click toggles `[visuallyselected]`
- On `fadeOut()`: selected → `gBrowser.addTrustedTab(url, {createLazyBrowser:true})` + `gZenPinnedTabManager.addToEssentials()` + `PlacesUtils.history.insertMany()` for visit records

---

## SCREEN 4 — Theme / Workspace Colors

### Copy
- Headline: `Your Workspaces, Your Colors`
- Body: `Personalize your browser by giving each workspace its own unique color identity.`
- CTA: `Next`

### Implementation: REUSES PRODUCTION THEME PANEL
Zen does NOT have a separate welcome-specific theme UI. It opens the production `#PanelUI-zen-gradient-generator` panel via `PanelMultiView.openPopup(panel, anchor, {position:"overlap"})` with `noautohide`/`consumeoutsideclicks=false`/`nonnativepopover`.

### Theme Picker Panel Structure (`--panel-width: 380px`)
```
PanelUI-zen-gradient-generator
└── view (.zen-theme-picker)
    ├── circular HSL gradient canvas (380×380px)
    │   ├── scheme buttons (auto/light/dark) — top:15px, 3 buttons × 30×30px
    │   ├── primary dot (38×38, border:6px solid #fff)
    │   ├── secondary dots (icon-size-medium, border:3px solid #fff)
    │   └── color action buttons (add/remove/cycle) — bottom
    ├── pagination L/R nav
    ├── 5 pages × 8 swatches grid
    └── controls
        ├── opacity slider (mac:0.30-0.90, other:0.25-0.90, default:0.5)
        └── texture rotator (5rem×5rem, 6rem on mac) — 16 dots, mix-blend-mode:hard-light
```

### Pre-seeded Swatches (5 pages × 8 = 40 colors)

**Page 1 — Light singles** (lightness in HSL):
- Cream `#f4efdf` (90)
- Pink `#f0b8cd` (80)
- Lavender `#e9c3e3` (80)
- Rose `#da7682` (70)
- Coral `#eb8570` (70)
- Gold `#dcce7f` (60)
- Mint `#5becad` (60)
- Slate `#919bb5` (50)

**Page 2 — Light analogous trios** (3 colors per swatch)

**Page 3 — Dark singles**:
- Dark purple `rgb(93,86,106)` (10)
- Mauve `#997096` (40)
- Dusty rose `#956066` (35)
- Burnt sienna `#9c6645` (30)
- Forest `#517b6c` (30)
- Teal `#576e75` (25)
- Brown `rgb(131,109,95)` (20)
- Dark green `#447464` (20)

**Page 4 — Dark analogous trios**

**Page 5 — Grayscale ramp** (9 steps, `rgb(224,224,224)` → `rgb(0,0,0)`)

### Gradient Algorithms (`getGradient()` from `ZenGradientGenerator.mjs`)
```
0 colors → fallback solid (rgba(0,0,0,.4) dark / #131313 dark-opaque / #e9e9e9 light)
1 color  → blendWithWhiteOverlay(opacity)
2 colors → background: linear-gradient(-45deg, c2 0%, transparent 100%)
                       + linear-gradient(135deg, c1 0%, transparent 100%)
            toolbar:   linear-gradient(-45deg, c2 0%, c1 100%)
3 colors → linear-gradient(-5deg, c1 10%, transparent 80%)
            + radial-gradient(circle at 95% 0%, c3 0%, transparent 75%)
            + radial-gradient(circle at 0% 0%, c2 10%, transparent 70%)
```

### Color Harmonies
```js
[
  { type: "complementary",      angles: [180] },
  { type: "singleAnalogous",    angles: [310] },
  { type: "splitComplementary", angles: [150, 210] },
  { type: "analogous",          angles: [50, 310] },
  { type: "triadic",            angles: [120, 240] },
  { type: "floating",           angles: [] }
]
```

### Theme Data Schema
```js
{
  type: "gradient",
  gradientColors: [{c:[r,g,b], isPrimary, isCustom, algorithm, lightness}, ...],
  opacity: 0.5,           // float 0.25-0.90
  texture: 0              // float 0-1 in 1/16 steps
}
```

### Dot Animation on Algorithm Switch
```js
animate(dot, {left:[..], top:[..]}, {duration:0.4, type:"spring", bounce:0.3});
```

---

## SCREEN 5 — Completion

### Copy
- Headline: `All set?<br/>Let's get rolling!` (two-line)
- Body: `You're all set up and ready to go. Click the button below to start browsing with Zen.`
- CTA: `Dive in!` (NO secondary button)

### Finish Sequence (`finish()` method)
```js
async finish() {
  // 1. Slide content pane right
  await animate("#zen-welcome-page-content", {x:[0,"100%"]}, {bounce:0});
  document.getElementById("zen-welcome-page-content").remove();

  // 2. Heart pulse
  await this.animHeart();

  // 3. Pin selected essentials
  await this.#pinRemainingTabs();

  // 4. Fade overlay
  await animate("#zen-welcome-pages", {opacity:[1,0]});
  document.getElementById("zen-welcome").remove();
  document.documentElement.removeAttribute("zen-welcome-stage");

  // 5. Reveal browser chrome (was display:none)
  for (const el of browserChildren) {
    el.style.opacity = 0;
    el.style.removeProperty("display");
  }
  gZenUIManager.updateTabsToolbar();

  // 6. Fade in chrome
  await animate(`#browser > *:not(background, toast)`, {opacity:[0,1]});

  // 7. Toast
  gZenUIManager.showToast("zen-welcome-finished");
}
```

### Heart Animation (1.5s + 0.2s delay = 1.7s total)
```js
animate("#zen-welcome-heart",
  { opacity:[0,1,1,1,0], scale:[0.5,1,1.2,1,1.2] },
  { duration:1.5, delay:0.2, bounce:0 });
```

### Heart CSS
```css
#zen-welcome-heart {
  width:100%; height:100%; opacity:0;
  background-image: url(chrome://browser/skin/zen-icons/essential-add.svg);
  background-size:15%; background-repeat:no-repeat; background-position:center;
  -moz-context-properties: fill, fill-opacity;
}
&[animate-heart] { overflow: hidden; }
```

### Toast Text
- `zen-welcome-finished`: `Your Zen has been set up correctly!`

---

## Theme Color System

### Hardcoded Branding
```css
--zen-branding-dark:  #101010;
--zen-branding-paper: #e2e2e2;
--zen-branding-bg:         light-dark(#e2e2e2, #101010);
--zen-branding-bg-reverse: light-dark(#101010, #e2e2e2);
```

### Derived from `--zen-primary-color`
```css
--zen-colors-primary:   light-dark(
  color-mix(in srgb, var(--zen-primary-color) 50%, black),
  color-mix(in srgb, var(--zen-primary-color) 20%, #101010 80%)
);
--zen-colors-secondary: light-dark(
  color-mix(in srgb, var(--zen-colors-primary) 20%, white 80%),
  color-mix(in srgb, var(--zen-primary-color) 30%, #101010 70%)
);
--zen-colors-tertiary:  light-dark(
  color-mix(in srgb, var(--zen-primary-color) 2%, white 98%),
  color-mix(in srgb, var(--zen-primary-color) 1%, #101010 99%)
);
--zen-colors-border: light-dark(
  color-mix(in srgb, --zen-colors-secondary 50%, transparent),
  color-mix(in srgb, --zen-colors-secondary 20%, rgb(79,79,79))
);
--zen-dialog-background: light-dark(#fafbff, #1c1c1c);
--zen-urlbar-background: light-dark(
  color-mix(in srgb, var(--zen-primary-color) 3%, #f4f4f4),
  color-mix(in srgb, var(--zen-primary-color) 4%, rgb(24,24,24))
);
--zen-main-browser-background: light-dark(rgb(235,235,235), #1b1b1b);
--zen-big-shadow: rgba(0,0,0,0.24) 0px 3px 8px;
```

### Light/Dark Scheme Files
```css
/* dark.inc.css */
color-scheme: dark;
--zen-urlbar-outline-offset: -2px;
--zen-urlbar-filter: blur(40px) saturate(110%) brightness(25%) contrast(100%);

/* light.inc.css */
color-scheme: light;
--zen-urlbar-outline-offset: 0px;
--zen-urlbar-filter: blur(40px) saturate(110%) brightness(75%) contrast(100%);
```

### Mode Pref
- `zen.view.window.scheme`: `0`=dark, `1`=light, `2`=auto

---

## Typography

| Use | Font Stack | Weight | Notes |
|---|---|---|---|
| Splash/branding | `"Zen-Junicode", serif` | 400 | Custom WOFF2, `font-feature-settings:"swsh" 1` |
| Italic | `"Zen-Junicode-Italic", serif` | 400 | |
| macOS UI | `SF Pro, ui-sans-serif, system-ui, ...` | — | `@media (-moz-platform: macos)` |
| Windows UI | `'Segoe UI', Tahoma, Geneva, ...` | — | `@media (-moz-platform: windows)` |

### Welcome Type Scale
| Context | size | weight | line-height |
|---|---|---|---|
| Splash title (override) | 5rem | 500 | 1.1 |
| `.zen-branding-title` base | 6rem | 400 | 0.9 |
| Step h1 | xx-large (~2rem) | 600 | — |
| Step body | inherited | normal | — |
| Theme picker label | 12px | 600 | — |
| Engine label | inherited | 600 | — |
| Button | inherited | 500 | — |

---

## Spacing

No formal grid var; uses `rem` (1rem = 16px):
- Sidebar padding: `3.8rem` (60.8px)
- Content gap: `1.6rem` (25.6px)
- Tab grid gap: `1rem` (16px)
- Button gap: `10px`
- Sidebar h1 margin-bottom: `1rem`
- Paragraph margin: `0 0 1.1rem 0` (17.6px)
- Mock browser sidebar padding: `1.4rem` (22.4px)
- Mac win button gap: `0.5rem` (8px)
- Engine grid padding: `5rem` (80px)
- Radio card padding: `1.5rem 1.1rem` (24px / 17.6px)

---

## Border Radius

| Token | Value | Applied to |
|---|---|---|
| `--zen-border-radius` | 7px | General |
| `--button-border-radius` | 8px | Buttons |
| `--toolbarbutton-border-radius` | 6px | Toolbar |
| Welcome page panel | `1em` (~16px) | `#zen-welcome-pages` |
| Mock browser | `1.2em` (~19.2px) | top-left only |
| Tab card | 1rem (16px) | `--border-radius-medium` |
| Radio card | 0.6rem (~9.6px) | Import labels |
| Theme dot (primary) | 50%, 38px | |
| Theme dot (secondary) | 50%, icon-size-medium | |

---

## Shadow

Single token everywhere:
```css
--zen-big-shadow: rgba(0, 0, 0, 0.24) 0px 3px 8px;
```

---

## Critical Implementation Notes

1. **No progress indicator** in Zen onboarding — no step counter, no progress bar.
2. **Theme picker is the production panel reused** — not a custom welcome UI.
3. **Default browser is on the Import screen**, side-by-side with import action (NOT a separate screen).
4. **Import delegates to Gecko's MigrationUtils** wizard — Zen does NOT custom-style migration UI.
5. **All animations use Motion One** spring physics, not CSS transitions, for page transitions.
6. **Window resize**: 875×560 initial; expandable for steps (no fullscreen lock).
7. **Heart icon** is `essential-add.svg` reused, not a custom heart asset.
8. **Toast on completion** uses standard `gZenUIManager.showToast(stringId)`.

---

## Maho Acceptance Criteria — Pixel-Level Parity Checklist

For each Maho-side equivalent screen, verify:

- [ ] Window initial size 875×560, centered
- [ ] Splash title at 5rem/500/1.1 line-height with Junicode-equivalent font
- [ ] Splash spans stagger 0.6s (span1 at 0.2s, span2 at 0.8s)
- [ ] Spring animation (stiffness:300, damping:20, mass:1.8) on splash entries
- [ ] Shell at 60×60% with 1em border-radius and standard shadow
- [ ] Sidebar 40%, padding 3.8rem
- [ ] Content area 60%, background rgba(0,0,0,.1) light / rgba(255,255,255,.1) dark
- [ ] Page transitions slide x:150% with spring (bounce:0.2 enter, 0:exit)
- [ ] Buttons stagger 0.1s with 0.4s startDelay on enter
- [ ] Import screen has TWO radio cards side-by-side (default + import action)
- [ ] Radio card: 1.5rem 1.1rem padding, 0.6rem radius, 2px border, scale:1.03 on checked
- [ ] Search engine grid 2×N, aspect 1/0.7, 5rem padding
- [ ] Essentials mock browser 70×80%, top-left radius 1.2em, 9 apps in 3-col grid
- [ ] Tab tile: 6rem height, 5rem min-width, 1rem radius, scale:1.06 on selected
- [ ] Theme picker uses production panel (or visually-identical custom)
- [ ] Theme uses gradient (not flat) backgrounds with opacity 0.5 default
- [ ] 5 pages × 8 swatches preset palette
- [ ] Completion: slide content right + heart pulse 1.5s + fade overlay + reveal chrome + toast
- [ ] Heart animation: opacity [0,1,1,1,0] scale [0.5,1,1.2,1,1.2] over 1.5s+0.2s delay
- [ ] All branding colors derived via `color-mix()` from --zen-primary-color
- [ ] Shadow exactly `rgba(0,0,0,.24) 0px 3px 8px`
- [ ] No progress indicator anywhere in flow
- [ ] Toast message: localized "set up correctly" string

---

## Source Permalinks

- ZenWelcome.mjs: https://github.com/zen-browser/desktop/blob/d5cbe55d2b16a599e59b2a25ae9df37270bb5bc4/src/zen/welcome/ZenWelcome.mjs
- zen-welcome.css: https://github.com/zen-browser/desktop/blob/d5cbe55d2b16a599e59b2a25ae9df37270bb5bc4/src/zen/welcome/zen-welcome.css
- zen-welcome.ftl: https://github.com/zen-browser/desktop/blob/d5cbe55d2b16a599e59b2a25ae9df37270bb5bc4/locales/en-US/browser/browser/zen-welcome.ftl
- zen-theme.css: https://github.com/zen-browser/desktop/blob/d5cbe55d2b16a599e59b2a25ae9df37270bb5bc4/src/zen/common/styles/zen-theme.css
- ZenGradientGenerator.mjs: https://github.com/zen-browser/desktop/blob/d5cbe55d2b16a599e59b2a25ae9df37270bb5bc4/src/zen/spaces/ZenGradientGenerator.mjs
